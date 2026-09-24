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
  GtkPositionType popup_side;  /* side of the button that faces the screen */
  WnckWindow *icon_window;     /* fallback apps: window whose icon is shown */
  GdkRectangle geometry;       /* last icon geometry set, in screen pixels */
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

/*
 * Tells the window manager where the button is, so minimize and restore
 * animations go to and from it (SPEC section 12). With force unset, nothing
 * is sent when the button has not moved.
 */
static void
update_icon_geometry (MockaDockButton *self,
                      gboolean         force)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  GPtrArray *windows = mocka_dock_app_get_windows (self->app);
  GtkAllocation allocation;
  GdkRectangle geometry;
  gint x, y, origin_x, origin_y, scale;
  guint i;

  if (!gtk_widget_get_mapped (widget)
      || !gtk_widget_translate_coordinates (widget, toplevel, 0, 0, &x, &y))
    return;

  gdk_window_get_origin (gtk_widget_get_window (toplevel), &origin_x, &origin_y);
  gtk_widget_get_allocation (widget, &allocation);
  scale = gtk_widget_get_scale_factor (widget);

  geometry.x = (origin_x + x) * scale;
  geometry.y = (origin_y + y) * scale;
  geometry.width = allocation.width * scale;
  geometry.height = allocation.height * scale;

  if (!force && gdk_rectangle_equal (&geometry, &self->geometry))
    return;
  self->geometry = geometry;

  for (i = 0; i < windows->len; i++)
    wnck_window_set_icon_geometry (g_ptr_array_index (windows, i),
                                   geometry.x, geometry.y,
                                   geometry.width, geometry.height);
}

static void
on_size_allocate (GtkWidget     *widget,
                  GtkAllocation *allocation,
                  gpointer       user_data)
{
  update_icon_geometry (MOCKA_DOCK_BUTTON (widget), FALSE);
}

static void
on_map (GtkWidget *widget,
        gpointer   user_data)
{
  update_icon_geometry (MOCKA_DOCK_BUTTON (widget), TRUE);
}

static void
on_windows_changed (MockaDockApp *app,
                    gpointer      user_data)
{
  MockaDockButton *self = MOCKA_DOCK_BUTTON (user_data);
  GPtrArray *windows = mocka_dock_app_get_windows (app);

  update_icon_geometry (self, TRUE);

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

/* Sets where popups open: the side of the button facing away from the panel. */
void
mocka_dock_button_set_popup_side (MockaDockButton *self,
                                  GtkPositionType  side)
{
  g_return_if_fail (MOCKA_IS_DOCK_BUTTON (self));

  self->popup_side = side;
}

/* Brings a window forward, switching to its workspace first if needed. */
static void
activate_window (WnckWindow *window,
                 guint32     time)
{
  WnckWorkspace *workspace = wnck_window_get_workspace (window);
  WnckScreen *screen = wnck_window_get_screen (window);

  if (workspace != NULL
      && workspace != wnck_screen_get_active_workspace (screen))
    wnck_workspace_activate (workspace, time);

  wnck_window_activate (window, time);
}

/* SPEC section 7: activate the window, or minimize it when it is focused. */
static void
toggle_window (WnckWindow *window,
               guint32     time)
{
  if (wnck_window_is_active (window))
    wnck_window_minimize (window);
  else
    activate_window (window, time);
}

static void
on_window_item_activate (GtkMenuItem *item,
                         gpointer     user_data)
{
  toggle_window (WNCK_WINDOW (user_data), gtk_get_current_event_time ());
}

static GtkWidget *
window_item_new (WnckWindow *window)
{
  GtkWidget *item = gtk_menu_item_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *label = gtk_label_new (wnck_window_get_name (window));

  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 50);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  gtk_container_add (GTK_CONTAINER (box),
                     gtk_image_new_from_pixbuf (wnck_window_get_mini_icon (window)));
  gtk_container_add (GTK_CONTAINER (box), label);
  gtk_container_add (GTK_CONTAINER (item), box);

  /* The window may close while the list is open. */
  g_signal_connect_object (item, "activate",
                           G_CALLBACK (on_window_item_activate), window, 0);

  return item;
}

static gboolean
destroy_menu (gpointer menu)
{
  gtk_widget_destroy (menu);
  return G_SOURCE_REMOVE;
}

static void
on_menu_deactivate (GtkMenuShell *menu,
                    gpointer      user_data)
{
  /* Items are activated after the menu deactivates, so destroy it later. */
  g_idle_add (destroy_menu, menu);
}

/*
 * Several windows: a list of their titles, standing in for the thumbnails
 * of SPEC section 8 until M4. Clicking the button again closes it, because
 * the open list takes that click.
 */
static void
show_window_list (MockaDockButton *self,
                  GPtrArray       *windows)
{
  GtkWidget *menu = gtk_menu_new ();
  GdkGravity button_anchor, menu_anchor;
  guint i;

  for (i = 0; i < windows->len; i++)
    gtk_menu_shell_append (GTK_MENU_SHELL (menu),
                           window_item_new (g_ptr_array_index (windows, i)));

  switch (self->popup_side)
    {
    case GTK_POS_BOTTOM:
      button_anchor = GDK_GRAVITY_SOUTH_WEST;
      menu_anchor = GDK_GRAVITY_NORTH_WEST;
      break;
    case GTK_POS_LEFT:
      button_anchor = GDK_GRAVITY_NORTH_WEST;
      menu_anchor = GDK_GRAVITY_NORTH_EAST;
      break;
    case GTK_POS_RIGHT:
      button_anchor = GDK_GRAVITY_NORTH_EAST;
      menu_anchor = GDK_GRAVITY_NORTH_WEST;
      break;
    case GTK_POS_TOP:
    default:
      button_anchor = GDK_GRAVITY_NORTH_WEST;
      menu_anchor = GDK_GRAVITY_SOUTH_WEST;
      break;
    }

  gtk_menu_attach_to_widget (GTK_MENU (menu), GTK_WIDGET (self), NULL);
  g_object_set (menu, "anchor-hints",
                GDK_ANCHOR_FLIP | GDK_ANCHOR_SLIDE | GDK_ANCHOR_RESIZE, NULL);
  g_signal_connect (menu, "deactivate", G_CALLBACK (on_menu_deactivate), NULL);
  gtk_widget_show_all (menu);
  gtk_menu_popup_at_widget (GTK_MENU (menu), GTK_WIDGET (self),
                            button_anchor, menu_anchor, NULL);
}

/* Plain left click (SPEC section 7). Launching waits for pinning (M2). */
static void
mocka_dock_button_clicked (GtkButton *button)
{
  MockaDockButton *self = MOCKA_DOCK_BUTTON (button);
  GPtrArray *windows = mocka_dock_app_get_windows (self->app);
  GdkModifierType state = 0;

  gtk_get_current_event_state (&state);
  if (state & gtk_accelerator_get_default_mod_mask ())
    return;

  if (windows->len == 1)
    toggle_window (g_ptr_array_index (windows, 0), gtk_get_current_event_time ());
  else if (windows->len > 1)
    show_window_list (self, windows);
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
  GtkButtonClass *button_class = GTK_BUTTON_CLASS (klass);

  object_class->dispose = mocka_dock_button_dispose;
  button_class->clicked = mocka_dock_button_clicked;
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
  g_signal_connect_after (self, "size-allocate",
                          G_CALLBACK (on_size_allocate), NULL);
  g_signal_connect_after (self, "map", G_CALLBACK (on_map), NULL);
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