/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include "probe.h"

#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#define PROBE_CAPTURE_WIDTH 320
#define PROBE_N_KEYS 10

static const KeySym probe_keys[PROBE_N_KEYS] = {
  XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9, XK_0,
};

static WnckHandle *probe_wnck;
static guint probe_super_mask;
static guint probe_numlock_mask;
static int probe_n_grabbed;
static gchar *probe_last_key;

/*
 * Most recently active window that a taskbar would show. The panel never
 * takes focus, so the active window is still the user's window when the
 * dock is clicked.
 */
static WnckWindow *
probe_recent_window (void)
{
  WnckScreen *screen = wnck_handle_get_default_screen (probe_wnck);
  WnckWindow *window;
  GList *l;

  window = wnck_screen_get_active_window (screen);
  if (window != NULL && !wnck_window_is_skip_tasklist (window))
    return window;

  /* Stacking order is bottom to top, so walk it backwards. */
  for (l = g_list_last (wnck_screen_get_windows_stacked (screen));
       l != NULL; l = l->prev)
    {
      window = l->data;
      if (!wnck_window_is_skip_tasklist (window)
          && wnck_window_get_window_type (window) == WNCK_WINDOW_NORMAL)
        return window;
    }

  return NULL;
}

/* A compositing manager owns the _NET_WM_CM_Sn selection (EWMH). */
static gboolean
probe_compositor_running (Display *dpy)
{
  char name[32];

  snprintf (name, sizeof name, "_NET_WM_CM_S%d", DefaultScreen (dpy));
  return XGetSelectionOwner (dpy, XInternAtom (dpy, name, False)) != None;
}

/*
 * Reads the window's contents through XRender. While a compositor redirects
 * the window, this reads its off-screen storage, so the window may be
 * covered by other windows.
 */
static cairo_surface_t *
probe_capture (GdkDisplay *display,
               Window      xid)
{
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  XWindowAttributes attr;
  cairo_surface_t *src;
  cairo_surface_t *dst;
  cairo_t *cr;
  double scale;

  gdk_x11_display_error_trap_push (display);

  if (!XGetWindowAttributes (dpy, xid, &attr) || attr.width <= 0)
    {
      gdk_x11_display_error_trap_pop_ignored (display);
      return NULL;
    }

  scale = MIN (1.0, (double) PROBE_CAPTURE_WIDTH / attr.width);
  src = cairo_xlib_surface_create (dpy, xid, attr.visual,
                                   attr.width, attr.height);
  dst = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
                                    MAX (1, (int) (attr.width * scale)),
                                    MAX (1, (int) (attr.height * scale)));

  cr = cairo_create (dst);
  cairo_scale (cr, scale, scale);
  cairo_set_source_surface (cr, src, 0, 0);
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (src);

  if (gdk_x11_display_error_trap_pop (display) != 0)
    {
      cairo_surface_destroy (dst);
      return NULL;
    }

  return dst;
}

static void
probe_show_capture (GtkMenuItem *item,
                    gpointer     user_data)
{
  GdkDisplay *display = gdk_display_get_default ();
  WnckWindow *window = probe_recent_window ();
  cairo_surface_t *surface = NULL;
  GtkWidget *dialog;
  GtkWidget *box;
  gchar *text;

  if (window != NULL)
    surface = probe_capture (display, wnck_window_get_xid (window));

  text = g_strdup_printf (_("Compositor: %s\nWindow: %s"),
      probe_compositor_running (gdk_x11_display_get_xdisplay (display))
        ? _("running") : _("not running"),
      window != NULL ? wnck_window_get_name (window) : _("none"));

  dialog = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (dialog), _("Capture (M0 test)"));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);
  gtk_container_add (GTK_CONTAINER (dialog), box);
  gtk_container_add (GTK_CONTAINER (box), gtk_label_new (text));

  if (surface != NULL)
    {
      gtk_container_add (GTK_CONTAINER (box), gtk_image_new_from_surface (surface));
      cairo_surface_destroy (surface);
    }
  else
    {
      gtk_container_add (GTK_CONTAINER (box), gtk_label_new (_("Capture failed")));
    }

  gtk_widget_show_all (dialog);
  g_free (text);
}

static void
probe_popup_menu (GtkWidget      *menu,
                  GtkWidget      *attach_widget,
                  GdkEventButton *event)
{
  gtk_menu_attach_to_widget (GTK_MENU (menu), attach_widget, NULL);
  g_signal_connect (menu, "selection-done",
                    G_CALLBACK (gtk_widget_destroy), NULL);
  gtk_widget_show_all (menu);
  gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) event);
}

static void
probe_menu_append_info (GtkWidget   *menu,
                        const gchar *text)
{
  GtkWidget *item = gtk_menu_item_new_with_label (text);

  gtk_widget_set_sensitive (item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

static GtkWidget *
probe_app_menu_new (void)
{
  GtkWidget *menu = gtk_menu_new ();
  GtkWidget *item;
  gchar *text;

  item = gtk_menu_item_new_with_label (_("Capture active window (M0 test)"));
  g_signal_connect (item, "activate", G_CALLBACK (probe_show_capture), NULL);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());

  text = g_strdup_printf (_("Shortcuts grabbed: %d of %d"),
                          probe_n_grabbed, PROBE_N_KEYS);
  probe_menu_append_info (menu, text);
  g_free (text);

  text = g_strdup_printf (_("Last shortcut: %s"),
                          probe_last_key != NULL ? probe_last_key : _("none"));
  probe_menu_append_info (menu, text);
  g_free (text);

  return menu;
}

/*
 * SPEC section 7: right click is consumed by the button, Ctrl + right click
 * reaches the panel's applet menu, and Shift + right click shows the window
 * menu.
 */
static gboolean
probe_button_press (GtkWidget      *button,
                    GdkEventButton *event,
                    gpointer        user_data)
{
  GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask ();
  WnckWindow *window;

  if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY)
    return FALSE;

  if (mods == GDK_CONTROL_MASK)
    return FALSE;

  if (mods == GDK_SHIFT_MASK)
    {
      window = probe_recent_window ();
      if (window != NULL)
        probe_popup_menu (wnck_action_menu_new (window), button, event);
      return TRUE;
    }

  if (mods == 0)
    {
      probe_popup_menu (probe_app_menu_new (), button, event);
      return TRUE;
    }

  return FALSE;
}

/* Modifier bit that a key such as Super_L or Num_Lock is mapped to. */
static guint
probe_modifier_mask (Display *dpy,
                     KeySym   keysym)
{
  XModifierKeymap *map = XGetModifierMapping (dpy);
  KeyCode code = XKeysymToKeycode (dpy, keysym);
  guint mask = 0;
  int i;

  for (i = 0; code != 0 && i < 8 * map->max_keypermod; i++)
    {
      if (map->modifiermap[i] == code)
        {
          mask = 1u << (i / map->max_keypermod);
          break;
        }
    }

  XFreeModifiermap (map);
  return mask;
}

static GdkFilterReturn
probe_key_filter (GdkXEvent *gdk_xevent,
                  GdkEvent  *event,
                  gpointer   user_data)
{
  XEvent *xevent = gdk_xevent;
  KeySym keysym;
  int i;

  if (xevent->type != KeyPress || !(xevent->xkey.state & probe_super_mask))
    return GDK_FILTER_CONTINUE;

  keysym = XLookupKeysym (&xevent->xkey, 0);
  for (i = 0; i < PROBE_N_KEYS; i++)
    {
      if (keysym == probe_keys[i])
        {
          g_free (probe_last_key);
          probe_last_key = g_strdup_printf ("Super+%d", (i + 1) % 10);
          g_message ("M0 probe: %s", probe_last_key);
          /* Readable with: xprop -root _MOCKA_DOCK_M0_PROBE */
          XChangeProperty (xevent->xkey.display, xevent->xkey.root,
                           XInternAtom (xevent->xkey.display,
                                        "_MOCKA_DOCK_M0_PROBE", False),
                           XA_STRING, 8, PropModeReplace,
                           (const guchar *) probe_last_key,
                           strlen (probe_last_key));
          return GDK_FILTER_REMOVE;
        }
    }

  return GDK_FILTER_CONTINUE;
}

/* Grabs Super + 1 to 9, 0 in every combination of CapsLock and NumLock. */
static void
probe_grab_keys (void)
{
  GdkDisplay *display = gdk_display_get_default ();
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  GdkWindow *root = gdk_screen_get_root_window (gdk_screen_get_default ());
  Window xroot = gdk_x11_window_get_xid (root);
  XWindowAttributes attr;
  guint locks[4];
  int i, j;

  probe_super_mask = probe_modifier_mask (dpy, XK_Super_L);
  if (probe_super_mask == 0)
    probe_super_mask = Mod4Mask;
  probe_numlock_mask = probe_modifier_mask (dpy, XK_Num_Lock);

  locks[0] = 0;
  locks[1] = LockMask;
  locks[2] = probe_numlock_mask;
  locks[3] = LockMask | probe_numlock_mask;

  for (i = 0; i < PROBE_N_KEYS; i++)
    {
      KeyCode code = XKeysymToKeycode (dpy, probe_keys[i]);

      if (code == 0)
        continue;

      gdk_x11_display_error_trap_push (display);
      for (j = 0; j < 4; j++)
        XGrabKey (dpy, code, probe_super_mask | locks[j], xroot, False,
                  GrabModeAsync, GrabModeAsync);
      if (gdk_x11_display_error_trap_pop (display) == 0)
        probe_n_grabbed++;
      else
        g_message ("M0 probe: Super+%d is taken", (i + 1) % 10);
    }

  /*
   * A menu bound to Super alone (Brisk Menu) holds the keyboard while Super
   * is down and re-sends other keys as synthetic events to the root window,
   * so the passive grab never fires. Listening on the root catches those.
   * Synthetic events only reach clients selecting core events, and GDK
   * selects XInput2 events, so select the core mask through Xlib.
   */
  XGetWindowAttributes (dpy, xroot, &attr);
  XSelectInput (dpy, xroot, attr.your_event_mask | KeyPressMask);
  gdk_window_add_filter (root, probe_key_filter, NULL);
  g_message ("M0 probe: grabbed %d of %d shortcuts", probe_n_grabbed,
             PROBE_N_KEYS);
}

void
probe_attach (GtkWidget  *button,
              WnckHandle *wnck)
{
  probe_wnck = wnck;

  g_signal_connect (button, "button-press-event",
                    G_CALLBACK (probe_button_press), NULL);
  probe_grab_keys ();
}
