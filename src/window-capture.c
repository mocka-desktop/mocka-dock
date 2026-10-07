/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Window contents for thumbnails (SPEC section 8). While a compositor runs,
 * every window is redirected to off-screen storage, so its contents can be
 * read through XRender even when other windows cover it. Without one, a
 * covered window would show what covers it, so nothing is captured then.
 *
 * The last capture of each window is kept as its snapshot, so a minimized
 * window, which has no contents to read, still shows how it looked.
 */

#include "config.h"

#include "window-capture.h"

#include <X11/Xlib.h>
#include <cairo-xlib.h>
#include <gdk/gdkx.h>

/* A compositing manager owns the _NET_WM_CM_Sn selection (EWMH). */
gboolean
mocka_compositor_running (GdkDisplay *display)
{
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  g_autofree gchar *name = g_strdup_printf ("_NET_WM_CM_S%d", DefaultScreen (dpy));

  return XGetSelectionOwner (dpy, XInternAtom (dpy, name, False)) != None;
}

/*
 * The window's contents scaled to fit width by height logical pixels,
 * keeping its proportions, at the given scale factor. The scaling happens
 * in the X server, so only the small image travels to the dock. NULL when
 * no compositor runs, or the window is not shown (minimized, or on another
 * workspace), or it went away meanwhile.
 */
cairo_surface_t *
mocka_window_capture (WnckWindow *window, gint width, gint height, gint scale)
{
  GdkDisplay *display = gdk_display_get_default ();
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  Window xid = wnck_window_get_xid (window);
  XWindowAttributes attributes;
  cairo_surface_t *source;
  cairo_surface_t *scaled;
  cairo_surface_t *image = NULL;
  cairo_t *cr;
  gdouble factor;
  gint pixel_width, pixel_height;

  if (!mocka_compositor_running (display) || wnck_window_is_minimized (window))
    return NULL;

  gdk_x11_display_error_trap_push (display);

  if (!XGetWindowAttributes (dpy, xid, &attributes) || attributes.map_state != IsViewable || attributes.width <= 0
      || attributes.height <= 0)
    {
      gdk_x11_display_error_trap_pop_ignored (display);
      return NULL;
    }

  factor = MIN ((gdouble)width * scale / attributes.width, (gdouble)height * scale / attributes.height);
  factor = MIN (factor, 1.0);
  pixel_width = MAX (1, (gint)(attributes.width * factor));
  pixel_height = MAX (1, (gint)(attributes.height * factor));

  source = cairo_xlib_surface_create (dpy, xid, attributes.visual, attributes.width, attributes.height);

  /* Scale into a small pixmap on the server, then read only that. */
  scaled = cairo_surface_create_similar (source, CAIRO_CONTENT_COLOR_ALPHA, pixel_width, pixel_height);
  cr = cairo_create (scaled);
  cairo_scale (cr, factor, factor);
  cairo_set_source_surface (cr, source, 0, 0);
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy (cr);

  image = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, pixel_width, pixel_height);
  cr = cairo_create (image);
  cairo_set_source_surface (cr, scaled, 0, 0);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy (cr);

  cairo_surface_destroy (scaled);
  cairo_surface_destroy (source);

  if (gdk_x11_display_error_trap_pop (display) != 0 || cairo_surface_status (image) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (image);
      return NULL;
    }

  cairo_surface_set_device_scale (image, scale, scale);
  return image;
}
/* Snapshots live on the window, and go with it. */
#define SNAPSHOT_KEY "mocka-dock-snapshot"

/* The scale factor of the primary monitor, which snapshots are taken at. */
static gint
snapshot_scale (GdkDisplay *display)
{
  GdkMonitor *monitor = gdk_display_get_primary_monitor (display);

  if (monitor == NULL)
    monitor = gdk_display_get_monitor (display, 0);
  return monitor != NULL ? gdk_monitor_get_scale_factor (monitor) : 1;
}

/*
 * Captures the window at the largest thumbnail size and keeps it as its
 * snapshot (SPEC section 8). Returns the snapshot, owned by the window, or
 * NULL when nothing could be captured; an earlier snapshot is kept then.
 */
cairo_surface_t *
mocka_window_take_snapshot (WnckWindow *window)
{
  cairo_surface_t *snapshot;

  g_return_val_if_fail (WNCK_IS_WINDOW (window), NULL);

  snapshot = mocka_window_capture (window, MOCKA_THUMBNAIL_WIDTH, MOCKA_THUMBNAIL_HEIGHT,
                                   snapshot_scale (gdk_display_get_default ()));
  if (snapshot == NULL)
    return NULL;

  g_object_set_data_full (G_OBJECT (window), SNAPSHOT_KEY, snapshot, (GDestroyNotify)cairo_surface_destroy);
  return snapshot;
}

/* The window's last snapshot, owned by the window, or NULL. */
cairo_surface_t *
mocka_window_get_snapshot (WnckWindow *window)
{
  g_return_val_if_fail (WNCK_IS_WINDOW (window), NULL);

  return g_object_get_data (G_OBJECT (window), SNAPSHOT_KEY);
}

/*
 * A new reference to the surface, scaled down to fit width by height
 * logical pixels when it is larger, keeping its proportions and its scale
 * factor.
 */
cairo_surface_t *
mocka_surface_fit (cairo_surface_t *surface, gint width, gint height)
{
  gdouble scale_x, scale_y, factor;
  gint pixel_width = cairo_image_surface_get_width (surface);
  gint pixel_height = cairo_image_surface_get_height (surface);
  cairo_surface_t *fitted;
  cairo_t *cr;

  cairo_surface_get_device_scale (surface, &scale_x, &scale_y);
  factor = MIN (width * scale_x / pixel_width, height * scale_y / pixel_height);
  if (factor >= 1.0)
    return cairo_surface_reference (surface);

  fitted = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, MAX (1, (gint)(pixel_width * factor)),
                                       MAX (1, (gint)(pixel_height * factor)));
  cr = cairo_create (fitted);
  cairo_scale (cr, factor, factor);
  cairo_set_source_surface (cr, surface, 0, 0);
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy (cr);

  cairo_surface_set_device_scale (fitted, scale_x, scale_y);
  return fitted;
}
