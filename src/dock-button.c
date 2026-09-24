/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * One dock button (SPEC section 5). Apps with a desktop entry show the
 * entry's icon from the icon theme, which GTK redraws when the theme or the
 * scale factor changes. Apps using the class fallback show the icon and
 * title of their first window (SPEC section 6, step 6).
 */

#include "config.h"

#include "dock-button.h"

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

struct _MockaDockButton
{
  GtkButton parent_instance;

  MockaDockApp *app;
  GtkWidget *image;
  gint size;
  WnckWindow *icon_window;  /* fallback apps: window whose icon is shown */
};

G_DEFINE_TYPE (MockaDockButton, mocka_dock_button, GTK_TYPE_BUTTON)

/* Icon size for a square button of this size, leaving room for the frame. */
static gint
icon_size_for (gint size)
{
  return CLAMP (size - 8, 12, 256);
}

static void
update_fallback_icon (MockaDockButton *self)
{
  gint scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));
  gint pixels = icon_size_for (self->size);
  g_autoptr(GdkPixbuf) scaled = NULL;
  cairo_surface_t *surface;
  GdkPixbuf *icon;

  if (self->icon_window == NULL)
    return;

  icon = wnck_window_get_icon (self->icon_window);
  if (icon == NULL)
    return;

  scaled = gdk_pixbuf_scale_simple (icon, pixels * scale, pixels * scale,
                                    GDK_INTERP_BILINEAR);
  surface = gdk_cairo_surface_create_from_pixbuf (scaled, scale, NULL);
  gtk_image_set_from_surface (GTK_IMAGE (self->image), surface);
  cairo_surface_destroy (surface);
}

static void
on_icon_changed (WnckWindow *window,
                 gpointer    user_data)
{
  update_fallback_icon (MOCKA_DOCK_BUTTON (user_data));
}

static void
on_name_changed (WnckWindow *window,
                 gpointer    user_data)
{
  gtk_widget_set_tooltip_text (GTK_WIDGET (user_data),
                               wnck_window_get_name (window));
}

static void
set_icon_window (MockaDockButton *self,
                 WnckWindow      *window)
{
  if (self->icon_window == window)
    return;

  if (self->icon_window != NULL)
    g_signal_handlers_disconnect_by_data (self->icon_window, self);

  self->icon_window = window;
  if (window == NULL)
    return;

  g_signal_connect (window, "icon-changed", G_CALLBACK (on_icon_changed), self);
  g_signal_connect (window, "name-changed", G_CALLBACK (on_name_changed), self);
  on_name_changed (window, self);
  update_fallback_icon (self);
}

static void
on_windows_changed (MockaDockApp *app,
                    gpointer      user_data)
{
  MockaDockButton *self = MOCKA_DOCK_BUTTON (user_data);
  GPtrArray *windows = mocka_dock_app_get_windows (app);

  if (mocka_dock_app_get_entry (app) != NULL)
    return;

  set_icon_window (self, windows->len > 0 ? g_ptr_array_index (windows, 0) : NULL);
}

/* Sets the length of a side of the square button, in pixels. */
void
mocka_dock_button_set_size (MockaDockButton *self,
                            gint             size)
{
  g_return_if_fail (MOCKA_IS_DOCK_BUTTON (self));

  if (self->size == size)
    return;

  self->size = size;
  gtk_widget_set_size_request (GTK_WIDGET (self), size, size);
  gtk_image_set_pixel_size (GTK_IMAGE (self->image), icon_size_for (size));
  update_fallback_icon (self);
}

MockaDockApp *
mocka_dock_button_get_app (MockaDockButton *self)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_BUTTON (self), NULL);

  return self->app;
}

static void
mocka_dock_button_scale_changed (GObject    *object,
                                 GParamSpec *pspec,
                                 gpointer    user_data)
{
  update_fallback_icon (MOCKA_DOCK_BUTTON (object));
}

static void
mocka_dock_button_dispose (GObject *object)
{
  MockaDockButton *self = MOCKA_DOCK_BUTTON (object);

  set_icon_window (self, NULL);
  if (self->app != NULL)
    g_signal_handlers_disconnect_by_data (self->app, self);
  g_clear_object (&self->app);

  G_OBJECT_CLASS (mocka_dock_button_parent_class)->dispose (object);
}

static void
mocka_dock_button_class_init (MockaDockButtonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = mocka_dock_button_dispose;
}

static void
mocka_dock_button_init (MockaDockButton *self)
{
  gtk_button_set_relief (GTK_BUTTON (self), GTK_RELIEF_NONE);
  gtk_widget_set_can_focus (GTK_WIDGET (self), FALSE);
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               "mocka-dock-button");

  self->image = gtk_image_new ();
  gtk_container_add (GTK_CONTAINER (self), self->image);

  g_signal_connect (self, "notify::scale-factor",
                    G_CALLBACK (mocka_dock_button_scale_changed), NULL);
}

GtkWidget *
mocka_dock_button_new (MockaDockApp *app)
{
  MockaDockButton *self;
  MockaAppEntry *entry;

  g_return_val_if_fail (MOCKA_IS_DOCK_APP (app), NULL);

  self = g_object_new (MOCKA_TYPE_DOCK_BUTTON, NULL);
  self->app = g_object_ref (app);
  entry = mocka_dock_app_get_entry (app);

  if (entry != NULL)
    {
      g_autoptr(GIcon) icon = NULL;

      /* Icon may be a theme name or an absolute path. */
      if (entry->icon != NULL)
        icon = g_icon_new_for_string (entry->icon, NULL);
      if (icon == NULL)
        icon = g_themed_icon_new ("application-x-executable");

      gtk_image_set_from_gicon (GTK_IMAGE (self->image), icon,
                                GTK_ICON_SIZE_BUTTON);
      gtk_widget_set_tooltip_text (GTK_WIDGET (self), entry->name);
    }

  g_signal_connect (app, "windows-changed",
                    G_CALLBACK (on_windows_changed), self);
  on_windows_changed (app, self);

  return GTK_WIDGET (self);
}