/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * One dock button (SPEC section 5). Apps with a desktop entry show the
 * entry's icon from the icon theme, which GTK redraws when the theme or the
 * scale factor changes. Apps using the class fallback show the icon and
 * title of their first window (SPEC section 6, step 7).
 */

#include "config.h"

#include "dock-button.h"

#include <math.h>
#include <string.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include "app-menu.h"

struct _MockaDockButton
{
  GtkButton parent_instance;

  MockaDockApp *app;
  GtkWidget *image;
  GtkGesture *middle_click;
  gint size;
  GtkPositionType popup_side;  /* side of the button that faces the screen */
  WnckWindow *icon_window;     /* fallback apps: window whose icon is shown */
  GdkRectangle geometry;       /* last icon geometry set, in screen pixels */
  guint pulse_id;              /* tick callback while the app is starting */
  gint64 pulse_start;          /* frame time the pulse started, in µs */
};

enum
{
  SIGNAL_LAUNCH,
  SIGNAL_PIN,
  SIGNAL_UNPIN,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

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
  gtk_widget_queue_draw (GTK_WIDGET (self));

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

/* The theme's highlight color (SPEC section 12). */
static void
get_highlight_color (GtkWidget *widget,
                     GdkRGBA   *color)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);

  if (!gtk_style_context_lookup_color (context, "theme_selected_bg_color", color)
      && !gtk_style_context_lookup_color (context, "selected_bg_color", color))
    gdk_rgba_parse (color, "#4a90d9");
}

/* The app has the focused window. */
static gboolean
is_active_app (MockaDockButton *self)
{
  GPtrArray *windows = mocka_dock_app_get_windows (self->app);
  guint i;

  for (i = 0; i < windows->len; i++)
    if (wnck_window_is_active (g_ptr_array_index (windows, i)))
      return TRUE;

  return FALSE;
}

/*
 * Running indicator: a bar on the edge of the button nearest the screen
 * edge, which is the side opposite to where popups open.
 */
static void
draw_bar (MockaDockButton *self,
          cairo_t         *cr,
          const GdkRGBA   *color)
{
  gint width = gtk_widget_get_allocated_width (GTK_WIDGET (self));
  gint height = gtk_widget_get_allocated_height (GTK_WIDGET (self));
  gint thickness = MAX (2, MIN (width, height) / 16);
  gint length;

  switch (self->popup_side)
    {
    case GTK_POS_BOTTOM:
      length = width * 3 / 5;
      cairo_rectangle (cr, (width - length) / 2, 0, length, thickness);
      break;
    case GTK_POS_LEFT:
      length = height * 3 / 5;
      cairo_rectangle (cr, width - thickness, (height - length) / 2,
                       thickness, length);
      break;
    case GTK_POS_RIGHT:
      length = height * 3 / 5;
      cairo_rectangle (cr, 0, (height - length) / 2, thickness, length);
      break;
    case GTK_POS_TOP:
    default:
      length = width * 3 / 5;
      cairo_rectangle (cr, (width - length) / 2, height - thickness,
                       length, thickness);
      break;
    }

  gdk_cairo_set_source_rgba (cr, color);
  cairo_fill (cr);
}

/*
 * The focused app's button gets a light fill of the highlight color behind
 * its icon; running apps get the bar on top.
 */
static gboolean
mocka_dock_button_draw (GtkWidget *widget,
                        cairo_t   *cr)
{
  MockaDockButton *self = MOCKA_DOCK_BUTTON (widget);
  GdkRGBA color;

  get_highlight_color (widget, &color);

  if (is_active_app (self))
    {
      cairo_save (cr);
      cairo_set_source_rgba (cr, color.red, color.green, color.blue,
                             color.alpha * 0.3);
      cairo_paint (cr);
      cairo_restore (cr);
    }

  GTK_WIDGET_CLASS (mocka_dock_button_parent_class)->draw (widget, cr);

  if (mocka_dock_app_get_windows (self->app)->len > 0)
    draw_bar (self, cr, &color);

  return FALSE;
}

/* Pulse period, in microseconds: about once a second (SPEC section 7). */
#define PULSE_PERIOD 1000000

/* The icon fades between full and faint once per period. */
static gboolean
on_pulse_tick (GtkWidget     *widget,
               GdkFrameClock *clock,
               gpointer       user_data)
{
  MockaDockButton *self = MOCKA_DOCK_BUTTON (widget);
  gint64 now = gdk_frame_clock_get_frame_time (clock);
  gdouble phase;

  if (self->pulse_start == 0)
    self->pulse_start = now;

  phase = (gdouble) ((now - self->pulse_start) % PULSE_PERIOD) / PULSE_PERIOD;
  gtk_widget_set_opacity (self->image, 0.65 + 0.35 * cos (2 * G_PI * phase));

  return G_SOURCE_CONTINUE;
}

/*
 * Pulses the icon while the app is starting (SPEC section 7). The
 * animation runs only then, so the dock stays idle otherwise.
 */
void
mocka_dock_button_set_launching (MockaDockButton *self,
                                 gboolean         launching)
{
  g_return_if_fail (MOCKA_IS_DOCK_BUTTON (self));

  if (launching == (self->pulse_id != 0))
    return;

  if (launching)
    {
      self->pulse_start = 0;
      self->pulse_id = gtk_widget_add_tick_callback (GTK_WIDGET (self),
                                                     on_pulse_tick, NULL, NULL);
    }
  else
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->pulse_id);
      self->pulse_id = 0;
      gtk_widget_set_opacity (self->image, 1.0);
    }
}

/* Sets where popups open: the side of the button facing away from the panel. */
void
mocka_dock_button_set_popup_side (MockaDockButton *self,
                                  GtkPositionType  side)
{
  g_return_if_fail (MOCKA_IS_DOCK_BUTTON (self));

  self->popup_side = side;
  gtk_widget_queue_draw (GTK_WIDGET (self));
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

/*
 * The most recently active of these windows: the highest in the stacking
 * order, since the window manager raises a window when it is activated.
 */
static WnckWindow *
most_recent_window (GPtrArray *windows)
{
  WnckScreen *screen = wnck_window_get_screen (g_ptr_array_index (windows, 0));
  GList *l;

  for (l = g_list_last (wnck_screen_get_windows_stacked (screen));
       l != NULL; l = l->prev)
    if (g_ptr_array_find (windows, l->data, NULL))
      return l->data;

  return g_ptr_array_index (windows, windows->len - 1);
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
  /*
   * Items are activated after the menu deactivates, so destroy it later.
   * Hold a reference: the button, and the menu with it, may already be
   * destroyed by then, for example after unpinning an app that is not
   * running.
   */
  g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, destroy_menu,
                   g_object_ref (menu), g_object_unref);
}

/* Opens a menu on the side of the button facing away from the panel. */
static void
popup_menu (MockaDockButton *self,
            GtkWidget       *menu)
{
  GdkGravity button_anchor, menu_anchor;

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
  guint i;

  for (i = 0; i < windows->len; i++)
    gtk_menu_shell_append (GTK_MENU_SHELL (menu),
                           window_item_new (g_ptr_array_index (windows, i)));

  popup_menu (self, menu);
}

/*
 * Right click opens the app menu (SPEC section 9.1). Ctrl + right click goes
 * on to the panel's menu (SPEC section 9.3); Shift + right click is the
 * window menu, still to come.
 */
static gboolean
on_button_press (GtkWidget      *widget,
                 GdkEventButton *event,
                 gpointer        user_data)
{
  MockaDockButton *self = MOCKA_DOCK_BUTTON (widget);
  GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask ();
  GtkWidget *menu;

  if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY
      || mods != 0)
    return FALSE;

  menu = mocka_app_menu_new (self);
  if (menu == NULL)
    return FALSE;

  popup_menu (self, menu);
  return TRUE;
}

/* The button's position on the screen, in logical pixels. */
gboolean
mocka_dock_button_get_screen_rect (MockaDockButton *self,
                                   GdkRectangle    *rect)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  gint x, y, origin_x, origin_y;

  g_return_val_if_fail (MOCKA_IS_DOCK_BUTTON (self), FALSE);

  if (!gtk_widget_get_mapped (widget)
      || !gtk_widget_translate_coordinates (widget, toplevel, 0, 0, &x, &y))
    return FALSE;

  gdk_window_get_origin (gtk_widget_get_window (toplevel), &origin_x, &origin_y);
  rect->x = origin_x + x;
  rect->y = origin_y + y;
  rect->width = gtk_widget_get_allocated_width (widget);
  rect->height = gtk_widget_get_allocated_height (widget);

  return TRUE;
}

/*
 * Asks for a new instance of the app through the "launch" signal; the
 * applet launches it, so the dock can recognize its windows by startup ID.
 * Apps using the class fallback have no desktop entry and cannot be
 * launched (SPEC section 6, step 7).
 */
static void
launch_new_instance (MockaDockButton *self)
{
  if (mocka_dock_app_get_entry (self->app) != NULL)
    g_signal_emit (self, signals[SIGNAL_LAUNCH], 0, NULL, NULL);
}

static void
on_middle_click_released (GtkGestureMultiPress *gesture,
                          gint                  n_press,
                          gdouble               x,
                          gdouble               y,
                          gpointer              user_data)
{
  GtkWidget *widget = GTK_WIDGET (user_data);

  /* Only when released over the button, like a normal click. */
  if (x >= 0 && y >= 0
      && x < gtk_widget_get_allocated_width (widget)
      && y < gtk_widget_get_allocated_height (widget))
    launch_new_instance (MOCKA_DOCK_BUTTON (widget));
}

/*
 * Left click (SPEC section 7). An app that is not running is launched, one
 * running only on other workspaces is switched to, and
 * Shift + click launches a new instance. Ctrl + click comes in M5.
 */
static void
mocka_dock_button_clicked (GtkButton *button)
{
  MockaDockButton *self = MOCKA_DOCK_BUTTON (button);
  GPtrArray *windows = mocka_dock_app_get_windows (self->app);
  GdkModifierType state = 0;
  GdkModifierType mods;

  gtk_get_current_event_state (&state);
  mods = state & gtk_accelerator_get_default_mod_mask ();

  if (mods == GDK_SHIFT_MASK)
    {
      launch_new_instance (self);
      return;
    }

  if (mods != 0)
    return;

  if (windows->len == 0)
    {
      GPtrArray *all = mocka_dock_app_get_all_windows (self->app);

      /* Pinned app running only on other workspaces (SPEC section 5). */
      if (all->len > 0)
        activate_window (most_recent_window (all), gtk_get_current_event_time ());
      else
        launch_new_instance (self);
    }
  else if (windows->len == 1)
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
  g_clear_object (&self->middle_click);
  if (self->app != NULL)
    g_signal_handlers_disconnect_by_data (self->app, self);
  g_clear_object (&self->app);

  G_OBJECT_CLASS (mocka_dock_button_parent_class)->dispose (object);
}

static void
mocka_dock_button_class_init (MockaDockButtonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkButtonClass *button_class = GTK_BUTTON_CLASS (klass);

  object_class->dispose = mocka_dock_button_dispose;

  signals[SIGNAL_LAUNCH] =
    g_signal_new ("launch", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 2,
                  G_TYPE_STRING, G_TYPE_STRV);
  signals[SIGNAL_PIN] =
    g_signal_new ("pin", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_UNPIN] =
    g_signal_new ("unpin", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  widget_class->draw = mocka_dock_button_draw;
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

  self->middle_click = gtk_gesture_multi_press_new (GTK_WIDGET (self));
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (self->middle_click),
                                 GDK_BUTTON_MIDDLE);
  g_signal_connect (self->middle_click, "released",
                    G_CALLBACK (on_middle_click_released), self);

  g_signal_connect (self, "notify::scale-factor",
                    G_CALLBACK (mocka_dock_button_scale_changed), NULL);
  g_signal_connect_after (self, "size-allocate",
                          G_CALLBACK (on_size_allocate), NULL);
  g_signal_connect_after (self, "map", G_CALLBACK (on_map), NULL);
  g_signal_connect (self, "button-press-event",
                    G_CALLBACK (on_button_press), NULL);
}

static const GtkTargetEntry drag_targets[] = {
  { (gchar *) MOCKA_DOCK_APP_TARGET, GTK_TARGET_SAME_APP, 0 },
};

/* The dragged data is the app's desktop entry ID. */
static void
on_drag_data_get (GtkWidget        *widget,
                  GdkDragContext   *context,
                  GtkSelectionData *data,
                  guint             info,
                  guint             time,
                  gpointer          user_data)
{
  MockaAppEntry *entry = mocka_dock_app_get_entry (MOCKA_DOCK_BUTTON (widget)->app);

  gtk_selection_data_set (data, gtk_selection_data_get_target (data), 8,
                          (const guchar *) entry->id, strlen (entry->id));
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

      /* Dragging moves or pins the app (SPEC section 10). */
      gtk_drag_source_set (GTK_WIDGET (self), GDK_BUTTON1_MASK,
                           drag_targets, G_N_ELEMENTS (drag_targets),
                           GDK_ACTION_MOVE);
      gtk_drag_source_set_icon_gicon (GTK_WIDGET (self), icon);
      g_signal_connect (self, "drag-data-get", G_CALLBACK (on_drag_data_get), NULL);
    }

  g_signal_connect (app, "windows-changed",
                    G_CALLBACK (on_windows_changed), self);
  on_windows_changed (app, self);

  return GTK_WIDGET (self);
}