/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <mate-panel-applet.h>
#include <mate-panel-applet-gsettings.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include "app-index.h"
#include "dock-button.h"
#include "desktop-import.h"
#include "dock-model.h"
#include "pinned-list.h"
#include "undo-popup.h"
#include "window-tracker.h"

#define MOCKA_DOCK_FACTORY_ID "MockaDockAppletFactory"
#define MOCKA_DOCK_APPLET_ID  "MockaDockApplet"

/* Size window icons are read at for fallback apps, before scaling. */
#define WINDOW_ICON_SIZE 96

#define MOCKA_TYPE_DOCK_APPLET (mocka_dock_applet_get_type ())
G_DECLARE_FINAL_TYPE (MockaDockApplet, mocka_dock_applet, MOCKA, DOCK_APPLET,
                      MatePanelApplet)

struct _MockaDockApplet
{
  MatePanelApplet parent_instance;

  GtkWidget *box;
  gint size;
  GtkPositionType popup_side;

  GSettings *settings;         /* per dock (SPEC section 16) */
  GSettings *shared_settings;  /* shared by all docks: pinned-apps */
  WnckHandle *wnck;
  MockaAppIndex *index;
  MockaDockModel *model;
  MockaWindowTracker *tracker;
  GtkWidget *undo_popup;       /* "<App> unpinned" popup, while it shows */

  /* Drag and drop (SPEC section 10). */
  guint drop_gap;              /* gap of the pinned apps to drop at */
  gboolean show_drop_marker;
};

G_DEFINE_TYPE (MockaDockApplet, mocka_dock_applet, PANEL_TYPE_APPLET)

static GtkOrientation
orientation_for_orient (MatePanelAppletOrient orient)
{
  switch (orient)
    {
    case MATE_PANEL_APPLET_ORIENT_LEFT:
    case MATE_PANEL_APPLET_ORIENT_RIGHT:
      return GTK_ORIENTATION_VERTICAL;
    case MATE_PANEL_APPLET_ORIENT_UP:
    case MATE_PANEL_APPLET_ORIENT_DOWN:
    default:
      return GTK_ORIENTATION_HORIZONTAL;
    }
}

/* The orient says which way the applet faces: UP on a bottom panel. */
static GtkPositionType
popup_side_for_orient (MatePanelAppletOrient orient)
{
  switch (orient)
    {
    case MATE_PANEL_APPLET_ORIENT_DOWN:
      return GTK_POS_BOTTOM;
    case MATE_PANEL_APPLET_ORIENT_LEFT:
      return GTK_POS_LEFT;
    case MATE_PANEL_APPLET_ORIENT_RIGHT:
      return GTK_POS_RIGHT;
    case MATE_PANEL_APPLET_ORIENT_UP:
    default:
      return GTK_POS_TOP;
    }
}

static void
set_button_popup_side (GtkWidget *button,
                       gpointer   user_data)
{
  mocka_dock_button_set_popup_side (MOCKA_DOCK_BUTTON (button),
                                    GPOINTER_TO_INT (user_data));
}

static void
apply_orient (MockaDockApplet       *self,
              MatePanelAppletOrient  orient)
{
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->box),
                                  orientation_for_orient (orient));
  self->popup_side = popup_side_for_orient (orient);
  gtk_container_foreach (GTK_CONTAINER (self->box), set_button_popup_side,
                         GINT_TO_POINTER (self->popup_side));
}

static void
mocka_dock_applet_change_orient (MatePanelApplet       *applet,
                                 MatePanelAppletOrient  orient)
{
  apply_orient (MOCKA_DOCK_APPLET (applet), orient);
}

static void
set_button_size (GtkWidget *button,
                 gpointer   user_data)
{
  mocka_dock_button_set_size (MOCKA_DOCK_BUTTON (button),
                              GPOINTER_TO_INT (user_data));
}

/* Buttons are square, as long on each side as the panel is thick. */
static void
mocka_dock_applet_change_size (MatePanelApplet *applet,
                               guint            size)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (applet);

  self->size = size;
  gtk_container_foreach (GTK_CONTAINER (self->box), set_button_size,
                         GINT_TO_POINTER (self->size));
}

/*
 * Launches through the tracker, which remembers the startup ID. action and
 * uris come from the app menu (SPEC section 9.1).
 */
static void
on_button_launch (MockaDockButton     *button,
                  const gchar         *action,
                  const gchar * const *uris,
                  gpointer             user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  MockaAppEntry *entry = mocka_dock_app_get_entry (mocka_dock_button_get_app (button));
  g_autoptr(GError) error = NULL;

  if (!mocka_window_tracker_launch (self->tracker, entry, action, uris,
                                    gtk_widget_get_display (GTK_WIDGET (button)),
                                    gtk_get_current_event_time (), &error))
    g_warning ("Cannot launch %s: %s", entry->id, error->message);
}

static void
close_undo_popup (MockaDockApplet *self)
{
  if (self->undo_popup != NULL)
    gtk_widget_destroy (self->undo_popup);
}

/*
 * Pins an app at a gap of the pinned list (SPEC section 10), or moves it
 * there when it is pinned already.
 */
static void
pin_at (MockaDockApplet *self,
        const gchar     *id,
        guint            gap)
{
  g_auto(GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");
  g_auto(GStrv) pinned = mocka_pinned_list_insert ((const gchar * const *) ids,
                                                   id, gap);

  g_settings_set_strv (self->shared_settings, "pinned-apps",
                       (const gchar * const *) pinned);
}

/* Pin to dock: added after the apps already pinned (SPEC section 5). */
static void
on_button_pin (MockaDockButton *button,
               gpointer         user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  MockaAppEntry *entry = mocka_dock_app_get_entry (mocka_dock_button_get_app (button));
  g_auto(GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");

  close_undo_popup (self);
  if (!g_strv_contains ((const gchar * const *) ids, entry->id))
    pin_at (self, entry->id, G_MAXUINT);
}

/* Undo: pins the app again at its previous position (SPEC section 10). */
static void
on_undo (MockaUndoPopup *popup,
         gpointer        user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  const gchar *id = g_object_get_data (G_OBJECT (popup), "desktop-id");
  guint position = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (popup), "position"));
  g_auto(GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");

  if (!g_strv_contains ((const gchar * const *) ids, id))
    pin_at (self, id, position);
}

/*
 * Unpin from dock, then the "<App> unpinned" popup with Undo where the
 * button was (SPEC section 10).
 */
static void
on_button_unpin (MockaDockButton *button,
                 gpointer         user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  MockaAppEntry *entry = mocka_dock_app_get_entry (mocka_dock_button_get_app (button));
  g_auto(GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");
  g_auto(GStrv) pinned = NULL;
  g_autofree gchar *id = g_strdup (entry->id);
  g_autofree gchar *name = g_strdup (entry->name != NULL ? entry->name : entry->id);
  GdkRectangle rect;
  gboolean have_rect;
  guint position;

  close_undo_popup (self);

  /* The button goes away with the setting change when the app is not running. */
  have_rect = mocka_dock_button_get_screen_rect (button, &rect);

  pinned = mocka_pinned_list_remove ((const gchar * const *) ids, id, &position);
  g_settings_set_strv (self->shared_settings, "pinned-apps",
                       (const gchar * const *) pinned);

  if (!have_rect)
    return;

  self->undo_popup = mocka_undo_popup_new (name);
  g_object_add_weak_pointer (G_OBJECT (self->undo_popup),
                             (gpointer *) &self->undo_popup);
  g_object_set_data_full (G_OBJECT (self->undo_popup), "desktop-id",
                          g_steal_pointer (&id), g_free);
  g_object_set_data (G_OBJECT (self->undo_popup), "position",
                     GUINT_TO_POINTER (position));
  g_signal_connect (self->undo_popup, "undo", G_CALLBACK (on_undo), self);
  mocka_undo_popup_show_at (MOCKA_UNDO_POPUP (self->undo_popup), &rect,
                            self->popup_side);
}

/* A click on the dock or a focus change counts as clicking elsewhere. */
static gboolean
on_dock_button_press (GtkWidget      *widget,
                      GdkEventButton *event,
                      gpointer        user_data)
{
  close_undo_popup (MOCKA_DOCK_APPLET (user_data));
  return FALSE;
}

static void
on_active_window_changed (WnckScreen *screen,
                          WnckWindow *previous,
                          gpointer    user_data)
{
  close_undo_popup (MOCKA_DOCK_APPLET (user_data));
}

/* Drag and drop onto the dock (SPEC section 10). */

enum
{
  DROP_TARGET_APP,
  DROP_TARGET_URI_LIST,
};

static const GtkTargetEntry drop_targets[] = {
  { (gchar *) MOCKA_DOCK_APP_TARGET, GTK_TARGET_SAME_APP, DROP_TARGET_APP },
  { (gchar *) "text/uri-list", 0, DROP_TARGET_URI_LIST },
};

/* Pinned apps come first in the dock, so they are its first buttons. */
static guint
count_pinned (MockaDockApplet *self)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->model));
  guint i;

  for (i = 0; i < n; i++)
    {
      g_autoptr(MockaDockApp) app = g_list_model_get_item (G_LIST_MODEL (self->model), i);

      if (!mocka_dock_app_get_pinned (app))
        break;
    }

  return i;
}

/* The coordinate along the dock's length. */
static gdouble
along (MockaDockApplet *self,
       gdouble          x,
       gdouble          y)
{
  return gtk_orientable_get_orientation (GTK_ORIENTABLE (self->box))
         == GTK_ORIENTATION_HORIZONTAL ? x : y;
}

/*
 * The gap of the pinned apps under the pointer, at x, y in the applet: 0
 * before the first, up to the number of pinned apps after the last.
 * in_running_area is set when the pointer is over the apps that are not
 * pinned.
 */
static guint
drop_gap_at (MockaDockApplet *self,
             gint             x,
             gint             y,
             gboolean        *in_running_area)
{
  g_autoptr(GList) children = gtk_container_get_children (GTK_CONTAINER (self->box));
  guint pinned = count_pinned (self);
  GList *l;
  guint i;

  *in_running_area = FALSE;

  for (l = children, i = 0; l != NULL; l = l->next, i++)
    {
      GtkWidget *child = l->data;
      gint cx, cy;

      gtk_widget_translate_coordinates (GTK_WIDGET (self), child, x, y, &cx, &cy);

      if (i == pinned)
        {
          *in_running_area = along (self, cx, cy) >= 0;
          return pinned;
        }

      if (along (self, cx, cy)
          < along (self, gtk_widget_get_allocated_width (child),
                   gtk_widget_get_allocated_height (child)) / 2.0)
        return i;
    }

  return pinned;
}

static void
set_drop_marker (MockaDockApplet *self,
                 gboolean         show)
{
  self->show_drop_marker = show;
  gtk_widget_queue_draw (self->box);
}

/* A line where the app will land, in the theme's highlight color. */
static gboolean
on_box_draw (GtkWidget *box,
             cairo_t   *cr,
             gpointer   user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  g_autoptr(GList) children = NULL;
  GtkStyleContext *context;
  GtkAllocation box_allocation, allocation;
  GtkWidget *child;
  GdkRGBA color;
  gboolean horizontal;
  gboolean after = FALSE;
  gint position = 0;

  if (!self->show_drop_marker)
    return FALSE;

  children = gtk_container_get_children (GTK_CONTAINER (box));
  child = g_list_nth_data (children, self->drop_gap);
  if (child == NULL && self->drop_gap > 0)
    {
      child = g_list_nth_data (children, self->drop_gap - 1);
      after = TRUE;
    }

  context = gtk_widget_get_style_context (box);
  if (!gtk_style_context_lookup_color (context, "theme_selected_bg_color", &color))
    gdk_rgba_parse (&color, "#4a90d9");
  gdk_cairo_set_source_rgba (cr, &color);

  gtk_widget_get_allocation (box, &box_allocation);
  horizontal = gtk_orientable_get_orientation (GTK_ORIENTABLE (box))
               == GTK_ORIENTATION_HORIZONTAL;

  if (child != NULL)
    {
      gtk_widget_get_allocation (child, &allocation);
      if (horizontal)
        position = allocation.x - box_allocation.x + (after ? allocation.width : 0);
      else
        position = allocation.y - box_allocation.y + (after ? allocation.height : 0);
    }

  if (horizontal)
    cairo_rectangle (cr, CLAMP (position - 1, 0, box_allocation.width - 2), 0,
                     2, box_allocation.height);
  else
    cairo_rectangle (cr, 0, CLAMP (position - 1, 0, box_allocation.height - 2),
                     box_allocation.width, 2);
  cairo_fill (cr);

  return FALSE;
}

/*
 * While dragging: dock buttons and desktop entry files are accepted. A
 * running app that is not pinned only pins when dropped among the pinned
 * apps.
 */
static gboolean
on_drag_motion (GtkWidget      *widget,
                GdkDragContext *context,
                gint            x,
                gint            y,
                guint           time,
                gpointer        user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  GtkWidget *source = gtk_drag_get_source_widget (context);
  gboolean in_running_area;

  if (gtk_drag_dest_find_target (widget, context, NULL) == GDK_NONE)
    {
      gdk_drag_status (context, 0, time);
      return FALSE;
    }

  self->drop_gap = drop_gap_at (self, x, y, &in_running_area);

  if (MOCKA_IS_DOCK_BUTTON (source) && in_running_area
      && !mocka_dock_app_get_pinned (mocka_dock_button_get_app (MOCKA_DOCK_BUTTON (source))))
    {
      set_drop_marker (self, FALSE);
      gdk_drag_status (context, 0, time);
      return TRUE;
    }

  set_drop_marker (self, TRUE);
  gdk_drag_status (context, source != NULL ? GDK_ACTION_MOVE : GDK_ACTION_COPY,
                   time);
  return TRUE;
}

/* Also emitted right before a drop, so drop_gap is kept. */
static void
on_drag_leave (GtkWidget      *widget,
               GdkDragContext *context,
               guint           time,
               gpointer        user_data)
{
  set_drop_marker (MOCKA_DOCK_APPLET (user_data), FALSE);
}

static gboolean
on_drag_drop (GtkWidget      *widget,
              GdkDragContext *context,
              gint            x,
              gint            y,
              guint           time,
              gpointer        user_data)
{
  GdkAtom target = gtk_drag_dest_find_target (widget, context, NULL);

  if (target == GDK_NONE)
    return FALSE;

  gtk_drag_get_data (widget, context, target, time);
  return TRUE;
}

/*
 * Pins each dropped desktop entry file, in order, from the drop gap on.
 * Files outside the applications directories are first copied into the
 * user's own. Returns the number pinned.
 */
static guint
pin_dropped_files (MockaDockApplet  *self,
                   gchar           **uris)
{
  const gchar * const *data_dirs = g_get_system_data_dirs ();
  g_autofree gchar *user_dir = g_build_filename (g_get_user_data_dir (),
                                                 "applications", NULL);
  g_autoptr(GPtrArray) app_dirs = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) ids = g_ptr_array_new_with_free_func (g_free);
  guint i;

  g_ptr_array_add (app_dirs, g_strdup (user_dir));
  for (i = 0; data_dirs[i] != NULL; i++)
    g_ptr_array_add (app_dirs, g_build_filename (data_dirs[i], "applications", NULL));
  g_ptr_array_add (app_dirs, NULL);

  for (i = 0; uris != NULL && uris[i] != NULL; i++)
    {
      g_autofree gchar *path = g_filename_from_uri (uris[i], NULL, NULL);
      g_autoptr(GError) error = NULL;
      gchar *id;

      if (path == NULL)
        continue;

      id = mocka_desktop_import (path, (const gchar * const *) app_dirs->pdata,
                                 user_dir, &error);
      if (id == NULL)
        {
          g_message ("Not pinning %s: %s", path, error->message);
          continue;
        }
      g_ptr_array_add (ids, id);
    }

  if (ids->len == 0)
    return 0;

  /* Copies are new applications; read them before pinning. */
  mocka_app_index_reload (self->index);

  for (i = 0; i < ids->len; i++)
    pin_at (self, g_ptr_array_index (ids, i), self->drop_gap + i);

  return ids->len;
}

static void
on_drag_data_received (GtkWidget        *widget,
                       GdkDragContext   *context,
                       gint              x,
                       gint              y,
                       GtkSelectionData *data,
                       guint             info,
                       guint             time,
                       gpointer          user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  gboolean success = FALSE;

  close_undo_popup (self);
  set_drop_marker (self, FALSE);

  if (info == DROP_TARGET_APP && gtk_selection_data_get_length (data) > 0)
    {
      g_autofree gchar *id = g_strndup ((const gchar *) gtk_selection_data_get_data (data),
                                        gtk_selection_data_get_length (data));

      pin_at (self, id, self->drop_gap);
      success = TRUE;
    }
  else if (info == DROP_TARGET_URI_LIST)
    {
      g_auto(GStrv) uris = gtk_selection_data_get_uris (data);

      success = pin_dropped_files (self, uris) > 0;
    }

  gtk_drag_finish (context, success, FALSE, time);
}

/* The button of an app the dock launched pulses while it starts. */
static void
on_launch_state_changed (MockaWindowTracker *tracker,
                         const gchar        *desktop_id,
                         gboolean            launching,
                         gpointer            user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  g_autoptr(GList) children = gtk_container_get_children (GTK_CONTAINER (self->box));
  GList *l;

  for (l = children; l != NULL; l = l->next)
    {
      MockaDockButton *button = MOCKA_DOCK_BUTTON (l->data);

      if (g_str_equal (mocka_dock_app_get_key (mocka_dock_button_get_app (button)),
                       desktop_id))
        mocka_dock_button_set_launching (button, launching);
    }
}

/* Keeps the buttons in the same order as the apps in the model. */
static void
on_items_changed (GListModel *list,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  GList *children = gtk_container_get_children (GTK_CONTAINER (self->box));
  GList *l = g_list_nth (children, position);
  guint i;

  for (i = 0; i < removed && l != NULL; i++, l = l->next)
    gtk_widget_destroy (l->data);
  g_list_free (children);

  for (i = 0; i < added; i++)
    {
      g_autoptr(MockaDockApp) app = g_list_model_get_item (list, position + i);
      GtkWidget *button = mocka_dock_button_new (app);

      mocka_dock_button_set_size (MOCKA_DOCK_BUTTON (button), self->size);
      mocka_dock_button_set_popup_side (MOCKA_DOCK_BUTTON (button),
                                        self->popup_side);
      /* The tracker adds the open windows while it is being created. */
      if (self->tracker != NULL)
        mocka_dock_button_set_launching (MOCKA_DOCK_BUTTON (button),
            mocka_window_tracker_is_launching (self->tracker,
                                               mocka_dock_app_get_key (app)));
      g_signal_connect (button, "launch", G_CALLBACK (on_button_launch), self);
      g_signal_connect (button, "pin", G_CALLBACK (on_button_pin), self);
      g_signal_connect (button, "unpin", G_CALLBACK (on_button_unpin), self);
      g_signal_connect (button, "button-press-event",
                        G_CALLBACK (on_dock_button_press), self);
      gtk_box_pack_start (GTK_BOX (self->box), button, FALSE, FALSE, 0);
      gtk_box_reorder_child (GTK_BOX (self->box), button, position + i);
      gtk_widget_show_all (button);
    }
}

static void
load_style (void)
{
  g_autoptr(GtkCssProvider) provider = gtk_css_provider_new ();

  /* Let the square size set the button size, not the theme's padding. */
  gtk_css_provider_load_from_data (provider,
      ".mocka-dock-button { padding: 0; min-width: 0; min-height: 0; }",
      -1, NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
      GTK_STYLE_PROVIDER (provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/*
 * Pinned apps from the shared pinned-apps setting (SPEC section 10). IDs of
 * apps that are not installed are kept in the setting but not shown.
 */
static void
update_pinned (MockaDockApplet *self)
{
  g_auto(GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");
  g_autoptr(GPtrArray) entries = g_ptr_array_new ();
  guint i;

  for (i = 0; ids[i] != NULL; i++)
    {
      MockaAppEntry *entry = mocka_app_index_lookup (self->index, ids[i]);

      if (entry != NULL)
        g_ptr_array_add (entries, entry);
    }

  mocka_dock_model_set_pinned (self->model, entries);
}

static void
mocka_dock_applet_dispose (GObject *object)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (object);

  close_undo_popup (self);
  g_clear_object (&self->settings);
  g_clear_object (&self->shared_settings);
  g_clear_object (&self->tracker);
  if (self->model != NULL)
    g_signal_handlers_disconnect_by_data (self->model, self);
  g_clear_object (&self->model);
  if (self->index != NULL)
    g_signal_handlers_disconnect_by_data (self->index, self);
  g_clear_object (&self->index);
  g_clear_object (&self->wnck);

  G_OBJECT_CLASS (mocka_dock_applet_parent_class)->dispose (object);
}

static void
mocka_dock_applet_class_init (MockaDockAppletClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MatePanelAppletClass *applet_class = MATE_PANEL_APPLET_CLASS (klass);

  object_class->dispose = mocka_dock_applet_dispose;
  applet_class->change_orient = mocka_dock_applet_change_orient;
  applet_class->change_size = mocka_dock_applet_change_size;
}

static void
mocka_dock_applet_init (MockaDockApplet *self)
{
  /* Window actions come from the user, so act as a pager (EWMH source
   * indication 2). */
  self->wnck = wnck_handle_new (WNCK_CLIENT_TYPE_PAGER);
  wnck_handle_set_default_icon_size (self->wnck, WINDOW_ICON_SIZE);

  self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add (GTK_CONTAINER (self), self->box);
}

static void
mocka_dock_applet_setup (MockaDockApplet *self)
{
  MatePanelApplet *applet = MATE_PANEL_APPLET (self);

  load_style ();

  mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MINOR);
  mate_panel_applet_set_background_widget (applet, GTK_WIDGET (self));

  self->size = mate_panel_applet_get_size (applet);
  apply_orient (self, mate_panel_applet_get_orient (applet));

  self->index = mocka_app_index_new_for_system ();
  self->model = mocka_dock_model_new ();
  g_signal_connect (self->model, "items-changed",
                    G_CALLBACK (on_items_changed), self);
  self->tracker = mocka_window_tracker_new (self->wnck, self->index,
                                            self->model);
  g_signal_connect (self->tracker, "launch-state-changed",
                    G_CALLBACK (on_launch_state_changed), self);

  self->settings = mate_panel_applet_settings_new (applet,
      (gchar *) "org.mocka_desktop.Dock.Instance");
  g_settings_bind (self->settings, "show-all-workspaces",
                   self->tracker, "show-all-workspaces", G_SETTINGS_BIND_GET);

  self->shared_settings = g_settings_new ("org.mocka_desktop.Dock");
  g_signal_connect_swapped (self->shared_settings, "changed::pinned-apps",
                            G_CALLBACK (update_pinned), self);
  g_signal_connect_swapped (self->index, "changed",
                            G_CALLBACK (update_pinned), self);
  update_pinned (self);

  /* The focused app's button is highlighted (SPEC section 12). */
  g_signal_connect_object (wnck_handle_get_default_screen (self->wnck),
                           "active-window-changed",
                           G_CALLBACK (gtk_widget_queue_draw), self->box,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (wnck_handle_get_default_screen (self->wnck),
                           "active-window-changed",
                           G_CALLBACK (on_active_window_changed), self, 0);

  gtk_drag_dest_set (GTK_WIDGET (self), 0, drop_targets,
                     G_N_ELEMENTS (drop_targets),
                     GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (self, "drag-motion", G_CALLBACK (on_drag_motion), self);
  g_signal_connect (self, "drag-leave", G_CALLBACK (on_drag_leave), self);
  g_signal_connect (self, "drag-drop", G_CALLBACK (on_drag_drop), self);
  g_signal_connect (self, "drag-data-received",
                    G_CALLBACK (on_drag_data_received), self);
  g_signal_connect_after (self->box, "draw", G_CALLBACK (on_box_draw), self);

  gtk_widget_show_all (GTK_WIDGET (self));
}

static gboolean
mocka_dock_applet_factory (MatePanelApplet *applet,
                           const gchar     *iid,
                           gpointer         user_data)
{
  if (strcmp (iid, MOCKA_DOCK_APPLET_ID) != 0)
    return FALSE;

  mocka_dock_applet_setup (MOCKA_DOCK_APPLET (applet));
  return TRUE;
}

/*
 * The dock runs inside mate-panel: the panel does not pass drag and drop on
 * to applets in their own process (SPEC section 10).
 */
MATE_PANEL_APPLET_IN_PROCESS_FACTORY (MOCKA_DOCK_FACTORY_ID,
                                      MOCKA_TYPE_DOCK_APPLET,
                                      "Mocka Dock",
                                      mocka_dock_applet_factory,
                                      NULL)