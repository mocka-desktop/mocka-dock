/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Thumbnail popup (SPEC section 8): one tile per window of the app under
 * the pointer, with the window's title and a close button. One popup serves
 * all the buttons of a dock, so moving from one button to the next switches
 * it at once. Tiles show the window's icon until window capture is added.
 */

#include "config.h"

#include "thumbnails.h"

#include <glib/gi18n-lib.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

/* Fixed delays, in milliseconds (SPEC section 8). */
#define SHOW_DELAY 400
#define HIDE_DELAY 300

/* Size of a window's preview, shrunk when many windows must fit. */
#define PREVIEW_WIDTH 192
#define PREVIEW_HEIGHT 120
#define MIN_PREVIEW_WIDTH 64

/* Room a tile takes besides its preview: padding, title and spacing. */
#define TILE_EXTRA_WIDTH 16
#define TILE_EXTRA_HEIGHT 40

/* Size of the window icon shown in place of its contents. */
#define ICON_SIZE 48

struct _MockaThumbnails
{
  GObject parent_instance;

  GtkWidget *dock;      /* the applet, for the panel's colors */
  GtkPositionType side; /* side of the buttons the popup opens on */
  GtkWidget *popup;
  GtkWidget *box;
  GtkCssProvider *colors;

  MockaDockButton *button; /* weak: the button shown, or about to be */
  MockaDockApp *app;       /* the app whose windows are shown */
  guint show_id;
  guint hide_id;
};

G_DEFINE_TYPE (MockaThumbnails, mocka_thumbnails, G_TYPE_OBJECT)

static void
set_button (MockaThumbnails *self, MockaDockButton *button)
{
  if (self->button == button)
    return;

  if (self->button != NULL)
    g_object_remove_weak_pointer (G_OBJECT (self->button), (gpointer *)&self->button);
  self->button = button;
  if (button != NULL)
    g_object_add_weak_pointer (G_OBJECT (button), (gpointer *)&self->button);
}

static void rebuild (MockaThumbnails *self);

static void
set_app (MockaThumbnails *self, MockaDockApp *app)
{
  if (self->app == app)
    return;

  if (self->app != NULL)
    g_signal_handlers_disconnect_by_data (self->app, self);
  /* NOLINTNEXTLINE(bugprone-sizeof-expression) the sizeof is inside g_set_object */
  g_set_object (&self->app, app);
  if (app != NULL)
    g_signal_connect_swapped (app, "windows-changed", G_CALLBACK (rebuild), self);
}

static void
cancel_timers (MockaThumbnails *self)
{
  g_clear_handle_id (&self->show_id, g_source_remove);
  g_clear_handle_id (&self->hide_id, g_source_remove);
}

/* Hides the popup and forgets the app it showed. */
void
mocka_thumbnails_hide (MockaThumbnails *self)
{
  g_return_if_fail (MOCKA_IS_THUMBNAILS (self));

  cancel_timers (self);
  gtk_widget_hide (self->popup);
  set_app (self, NULL);
  set_button (self, NULL);
}

static gboolean
is_showing (MockaThumbnails *self)
{
  return gtk_widget_get_visible (self->popup);
}

/* The window's icon, centered in a preview of the given size. */
static void
set_icon_preview (GtkWidget *preview, WnckWindow *window)
{
  gint scale = gtk_widget_get_scale_factor (preview);
  GdkPixbuf *icon = wnck_window_get_icon (window);
  g_autoptr (GdkPixbuf) scaled = NULL;
  cairo_surface_t *surface;

  if (icon == NULL)
    return;

  scaled = gdk_pixbuf_scale_simple (icon, ICON_SIZE * scale, ICON_SIZE * scale, GDK_INTERP_BILINEAR);
  surface = gdk_cairo_surface_create_from_pixbuf (scaled, scale, NULL);
  gtk_image_set_from_surface (GTK_IMAGE (preview), surface);
  cairo_surface_destroy (surface);
}

static void
on_window_name_changed (WnckWindow *window, gpointer label)
{
  gtk_label_set_text (GTK_LABEL (label), wnck_window_get_name (window));
}

static void
on_window_name_changed_tooltip (WnckWindow *window, gpointer tile)
{
  gtk_widget_set_tooltip_text (GTK_WIDGET (tile), wnck_window_get_name (window));
}

static void
on_window_icon_changed (WnckWindow *window, gpointer preview)
{
  set_icon_preview (GTK_WIDGET (preview), window);
}

/*
 * Clicking a tile activates its window, or minimizes it when it is the
 * focused window (SPEC section 8).
 */
static void
on_tile_clicked (GtkButton *tile, gpointer user_data)
{
  MockaThumbnails *self = MOCKA_THUMBNAILS (user_data);
  WnckWindow *window = g_object_get_data (G_OBJECT (tile), "window");

  mocka_dock_toggle_window (window, gtk_get_current_event_time ());
  mocka_thumbnails_hide (self);
}

/* The popup follows the app's windows, and hides after the last one. */
static void
on_close_clicked (GtkButton *close, gpointer user_data)
{
  wnck_window_close (WNCK_WINDOW (user_data), gtk_get_current_event_time ());
}

/* The close button shows while the pointer is over its tile. */
static gboolean
on_tile_enter (GtkWidget *widget, GdkEventCrossing *event, gpointer close)
{
  gtk_widget_show (GTK_WIDGET (close));
  return FALSE;
}

/*
 * The tile and its close button are side by side for input, so leaving one
 * for the other is a leave too: hide only when the pointer is off the tile.
 */
static gboolean
on_tile_leave (GtkWidget *widget, GdkEventCrossing *event, gpointer close)
{
  GtkWidget *overlay = gtk_widget_get_parent (GTK_WIDGET (close));
  gint x, y;

  if (gtk_widget_translate_coordinates (widget, overlay, (gint)event->x, (gint)event->y, &x, &y) && x >= 0 && y >= 0
      && x < gtk_widget_get_allocated_width (overlay) && y < gtk_widget_get_allocated_height (overlay))
    return FALSE;

  gtk_widget_hide (GTK_WIDGET (close));
  return FALSE;
}

static GtkWidget *
tile_new (MockaThumbnails *self, WnckWindow *window, gint width, gint height)
{
  GtkWidget *overlay = gtk_overlay_new ();
  GtkWidget *tile = gtk_button_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *preview = gtk_image_new ();
  GtkWidget *label = gtk_label_new (wnck_window_get_name (window));
  GtkWidget *close = gtk_button_new_from_icon_name ("window-close-symbolic", GTK_ICON_SIZE_MENU);

  gtk_widget_set_size_request (preview, width, height);
  set_icon_preview (preview, window);

  /* The title takes the preview's width and is shortened to fit it. */
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 1);

  gtk_container_add (GTK_CONTAINER (box), preview);
  gtk_container_add (GTK_CONTAINER (box), label);

  gtk_button_set_relief (GTK_BUTTON (tile), GTK_RELIEF_NONE);
  gtk_widget_set_can_focus (tile, FALSE);
  gtk_widget_set_tooltip_text (tile, wnck_window_get_name (window));
  gtk_container_add (GTK_CONTAINER (tile), box);
  g_object_set_data_full (G_OBJECT (tile), "window", g_object_ref (window), g_object_unref);
  g_signal_connect (tile, "clicked", G_CALLBACK (on_tile_clicked), self);

  gtk_button_set_relief (GTK_BUTTON (close), GTK_RELIEF_NONE);
  gtk_widget_set_can_focus (close, FALSE);
  gtk_widget_set_tooltip_text (close, _ ("Close window"));
  gtk_widget_set_halign (close, GTK_ALIGN_END);
  gtk_widget_set_valign (close, GTK_ALIGN_START);
  gtk_widget_set_no_show_all (close, TRUE);
  g_signal_connect_object (close, "clicked", G_CALLBACK (on_close_clicked), window, 0);

  gtk_container_add (GTK_CONTAINER (overlay), tile);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), close);

  g_signal_connect (tile, "enter-notify-event", G_CALLBACK (on_tile_enter), close);
  g_signal_connect (tile, "leave-notify-event", G_CALLBACK (on_tile_leave), close);
  g_signal_connect (close, "enter-notify-event", G_CALLBACK (on_tile_enter), close);
  g_signal_connect (close, "leave-notify-event", G_CALLBACK (on_tile_leave), close);

  g_signal_connect_object (window, "name-changed", G_CALLBACK (on_window_name_changed), label, 0);
  g_signal_connect_object (window, "name-changed", G_CALLBACK (on_window_name_changed_tooltip), tile, 0);
  g_signal_connect_object (window, "icon-changed", G_CALLBACK (on_window_icon_changed), preview, 0);

  return overlay;
}

static gboolean
is_horizontal (MockaThumbnails *self)
{
  return self->side == GTK_POS_TOP || self->side == GTK_POS_BOTTOM;
}

/* The monitor the button is on, with its geometry. */
static GdkMonitor *
button_monitor (MockaThumbnails *self, const GdkRectangle *button, GdkRectangle *geometry)
{
  GdkDisplay *display = gtk_widget_get_display (self->popup);
  GdkMonitor *monitor
      = gdk_display_get_monitor_at_point (display, button->x + button->width / 2, button->y + button->height / 2);

  gdk_monitor_get_geometry (monitor, geometry);
  return monitor;
}

/*
 * Preview size for n windows: the full size, or smaller so all the tiles fit
 * in nine tenths of the monitor's length along the dock.
 */
static void
preview_size (MockaThumbnails *self, const GdkRectangle *monitor, guint n, gint *width, gint *height)
{
  gint room;

  if (is_horizontal (self))
    {
      room = monitor->width * 9 / 10 / MAX ((gint)n, 1) - TILE_EXTRA_WIDTH;
      *width = CLAMP (room, MIN_PREVIEW_WIDTH, PREVIEW_WIDTH);
      *height = *width * PREVIEW_HEIGHT / PREVIEW_WIDTH;
    }
  else
    {
      room = monitor->height * 9 / 10 / MAX ((gint)n, 1) - TILE_EXTRA_HEIGHT;
      *height = CLAMP (room, MIN_PREVIEW_WIDTH * PREVIEW_HEIGHT / PREVIEW_WIDTH, PREVIEW_HEIGHT);
      *width = *height * PREVIEW_WIDTH / PREVIEW_HEIGHT;
    }
}

/*
 * Next to the button on the side facing the screen, centered on it, and
 * kept within its monitor (SPEC section 8).
 */
static void
place (MockaThumbnails *self, const GdkRectangle *button, const GdkRectangle *monitor)
{
  GtkRequisition size;
  gint x, y;

  gtk_widget_get_preferred_size (self->popup, NULL, &size);
  gtk_window_resize (GTK_WINDOW (self->popup), size.width, size.height);

  switch (self->side)
    {
    case GTK_POS_BOTTOM:
      x = button->x + (button->width - size.width) / 2;
      y = button->y + button->height;
      break;
    case GTK_POS_LEFT:
      x = button->x - size.width;
      y = button->y + (button->height - size.height) / 2;
      break;
    case GTK_POS_RIGHT:
      x = button->x + button->width;
      y = button->y + (button->height - size.height) / 2;
      break;
    case GTK_POS_TOP:
    default:
      x = button->x + (button->width - size.width) / 2;
      y = button->y - size.height;
      break;
    }

  x = CLAMP (x, monitor->x, monitor->x + monitor->width - size.width);
  y = CLAMP (y, monitor->y, monitor->y + monitor->height - size.height);
  gtk_window_move (GTK_WINDOW (self->popup), x, y);
}

/* Tiles for the app's windows, placed next to its button. */
static void
rebuild (MockaThumbnails *self)
{
  g_autoptr (GList) children = gtk_container_get_children (GTK_CONTAINER (self->box));
  GdkRectangle button, monitor;
  GPtrArray *windows;
  GList *l;
  gint width, height;
  guint i;

  if (self->button == NULL || self->app == NULL || !mocka_dock_button_get_screen_rect (self->button, &button))
    {
      mocka_thumbnails_hide (self);
      return;
    }

  windows = mocka_dock_app_get_windows (self->app);
  if (windows->len == 0)
    {
      mocka_thumbnails_hide (self);
      return;
    }

  for (l = children; l != NULL; l = l->next)
    gtk_widget_destroy (l->data);

  button_monitor (self, &button, &monitor);
  preview_size (self, &monitor, windows->len, &width, &height);

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->box),
                                  is_horizontal (self) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
  for (i = 0; i < windows->len; i++)
    gtk_container_add (GTK_CONTAINER (self->box), tile_new (self, g_ptr_array_index (windows, i), width, height));
  gtk_widget_show_all (self->box);

  place (self, &button, &monitor);
}

/*
 * The panel's background and text colors (SPEC section 8). A see-through
 * background is only kept while a compositor can show it.
 */
static void
apply_panel_colors (MockaThumbnails *self)
{
  GtkStyleContext *context = gtk_widget_get_style_context (gtk_widget_get_toplevel (self->dock));
  GtkStateFlags state = gtk_style_context_get_state (context);
  GdkRGBA *background = NULL;
  GdkRGBA color;
  g_autofree gchar *background_css = NULL;
  g_autofree gchar *color_css = NULL;
  g_autofree gchar *css = NULL;

  gtk_style_context_get (context, state, "background-color", &background, NULL);
  gtk_style_context_get_color (context, state, &color);

  /* A panel without a background of its own shows the theme's. */
  if (background->alpha < 0.1 && !gtk_style_context_lookup_color (context, "theme_bg_color", background))
    gdk_rgba_parse (background, "#3c3c3c");
  if (!gdk_screen_is_composited (gtk_widget_get_screen (self->popup)))
    background->alpha = 1.0;

  background_css = gdk_rgba_to_string (background);
  color_css = gdk_rgba_to_string (&color);
  css = g_strdup_printf (".mocka-dock-thumbnails { background-color: %s; color: %s;"
                         " padding: 4px; }",
                         background_css, color_css);
  gtk_css_provider_load_from_data (self->colors, css, -1, NULL);
  gdk_rgba_free (background);
}

static void
show_for (MockaThumbnails *self, MockaDockButton *button)
{
  cancel_timers (self);

  if (mocka_dock_app_get_windows (mocka_dock_button_get_app (button))->len == 0)
    {
      mocka_thumbnails_hide (self);
      return;
    }

  set_button (self, button);
  set_app (self, mocka_dock_button_get_app (button));
  apply_panel_colors (self);
  rebuild (self);

  if (self->app != NULL)
    gtk_widget_show (self->popup);
}

static gboolean
on_show_timeout (gpointer user_data)
{
  MockaThumbnails *self = MOCKA_THUMBNAILS (user_data);

  self->show_id = 0;
  if (self->button != NULL)
    show_for (self, self->button);

  return G_SOURCE_REMOVE;
}

static gboolean
on_hide_timeout (gpointer user_data)
{
  MockaThumbnails *self = MOCKA_THUMBNAILS (user_data);

  self->hide_id = 0;
  mocka_thumbnails_hide (self);

  return G_SOURCE_REMOVE;
}

static void
start_hide (MockaThumbnails *self)
{
  g_clear_handle_id (&self->hide_id, g_source_remove);
  if (is_showing (self))
    self->hide_id = g_timeout_add (HIDE_DELAY, on_hide_timeout, self);
}

/*
 * The pointer entered a button: the popup shows after the delay, or at once
 * when it is already showing for another button.
 */
void
mocka_thumbnails_enter (MockaThumbnails *self, MockaDockButton *button)
{
  g_return_if_fail (MOCKA_IS_THUMBNAILS (self));

  cancel_timers (self);

  if (is_showing (self))
    {
      show_for (self, button);
      return;
    }

  if (mocka_dock_app_get_windows (mocka_dock_button_get_app (button))->len == 0)
    return;

  set_button (self, button);
  self->show_id = g_timeout_add (SHOW_DELAY, on_show_timeout, self);
}

/* The pointer left a button: a grace period lets it reach the popup. */
void
mocka_thumbnails_leave (MockaThumbnails *self)
{
  g_return_if_fail (MOCKA_IS_THUMBNAILS (self));

  g_clear_handle_id (&self->show_id, g_source_remove);
  start_hide (self);
}

/* A click on a button cancels a popup that has not shown yet. */
void
mocka_thumbnails_cancel (MockaThumbnails *self)
{
  g_return_if_fail (MOCKA_IS_THUMBNAILS (self));

  g_clear_handle_id (&self->show_id, g_source_remove);
}

/* Left click on an app with several windows (SPEC section 7). */
void
mocka_thumbnails_toggle (MockaThumbnails *self, MockaDockButton *button)
{
  g_return_if_fail (MOCKA_IS_THUMBNAILS (self));

  if (is_showing (self) && self->button == button)
    mocka_thumbnails_hide (self);
  else
    show_for (self, button);
}

/* Sets the side of the buttons that faces the screen. */
void
mocka_thumbnails_set_side (MockaThumbnails *self, GtkPositionType side)
{
  g_return_if_fail (MOCKA_IS_THUMBNAILS (self));

  self->side = side;
  mocka_thumbnails_hide (self);
}

static gboolean
on_popup_enter (GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  g_clear_handle_id (&MOCKA_THUMBNAILS (user_data)->hide_id, g_source_remove);
  return FALSE;
}

static gboolean
on_popup_leave (GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  if (event->detail != GDK_NOTIFY_INFERIOR)
    start_hide (MOCKA_THUMBNAILS (user_data));
  return FALSE;
}

static void
mocka_thumbnails_dispose (GObject *object)
{
  MockaThumbnails *self = MOCKA_THUMBNAILS (object);

  cancel_timers (self);
  set_app (self, NULL);
  set_button (self, NULL);
  g_clear_pointer (&self->popup, gtk_widget_destroy);
  g_clear_object (&self->colors);

  G_OBJECT_CLASS (mocka_thumbnails_parent_class)->dispose (object);
}

static void
mocka_thumbnails_class_init (MockaThumbnailsClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = mocka_thumbnails_dispose;
}

static void
mocka_thumbnails_init (MockaThumbnails *self)
{
  GdkScreen *screen = gdk_screen_get_default ();
  GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
  GtkStyleContext *context;

  self->side = GTK_POS_TOP;
  self->colors = gtk_css_provider_new ();

  self->popup = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_window_set_type_hint (GTK_WINDOW (self->popup), GDK_WINDOW_TYPE_HINT_TOOLTIP);
  gtk_window_set_resizable (GTK_WINDOW (self->popup), FALSE);
  if (visual != NULL)
    gtk_widget_set_visual (self->popup, visual);

  context = gtk_widget_get_style_context (self->popup);
  gtk_style_context_add_class (context, "mocka-dock-thumbnails");
  gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (self->colors), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_widget_add_events (self->popup, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (self->popup, "enter-notify-event", G_CALLBACK (on_popup_enter), self);
  g_signal_connect (self->popup, "leave-notify-event", G_CALLBACK (on_popup_leave), self);

  self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_container_add (GTK_CONTAINER (self->popup), self->box);
}

/* A popup for the buttons of a dock, which gives it the panel's colors. */
MockaThumbnails *
mocka_thumbnails_new (GtkWidget *dock)
{
  MockaThumbnails *self;

  g_return_val_if_fail (GTK_IS_WIDGET (dock), NULL);

  self = g_object_new (MOCKA_TYPE_THUMBNAILS, NULL);
  self->dock = dock;

  return self;
}