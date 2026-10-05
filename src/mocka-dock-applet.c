/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <mate-panel-applet-gsettings.h>
#include <mate-panel-applet.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include "app-index.h"
#include "desktop-import.h"
#include "dock-button.h"
#include "dock-model.h"
#include "pinned-list.h"
#include "thumbnails.h"
#include "undo-popup.h"
#include "window-tracker.h"

#define MOCKA_DOCK_FACTORY_ID "MockaDockAppletFactory"
#define MOCKA_DOCK_APPLET_ID "MockaDockApplet"

/* Size window icons are read at for fallback apps, before scaling. */
#define WINDOW_ICON_SIZE 96

#define MOCKA_TYPE_DOCK_APPLET (mocka_dock_applet_get_type ())
G_DECLARE_FINAL_TYPE (MockaDockApplet, mocka_dock_applet, MOCKA, DOCK_APPLET, MatePanelApplet)

struct _MockaDockApplet
{
  MatePanelApplet parent_instance;

  GtkWidget *outer;    /* start arrow, scroller, end arrow */
  GtkWidget *scroller; /* scrolls the buttons when they do not fit */
  GtkWidget *box;      /* the app buttons */
  GtkWidget *arrow_start;
  GtkWidget *arrow_end;
  gint size_hints[2];           /* kept: the panel reads them later */
  GSettings *object_settings;   /* the panel's settings for the dock */
  GSettings *toplevel_settings; /* the panel's own settings */
  gint size;
  GtkPositionType popup_side;

  GSettings *settings;        /* per dock (SPEC section 16) */
  GSettings *shared_settings; /* shared by all docks: pinned-apps */
  WnckHandle *wnck;
  MockaAppIndex *index;
  MockaDockModel *model;
  MockaWindowTracker *tracker;
  GtkWidget *undo_popup;       /* "<App> unpinned" popup, while it shows */
  MockaThumbnails *thumbnails; /* window thumbnails of the hovered app */

  /* Drag and drop (SPEC section 10). */
  guint drop_gap; /* gap of the pinned apps to drop at */
  gboolean show_drop_marker;
};

G_DEFINE_TYPE (MockaDockApplet, mocka_dock_applet, PANEL_TYPE_APPLET)

static void update_size_hints (MockaDockApplet *self);

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
set_button_popup_side (GtkWidget *button, gpointer user_data)
{
  mocka_dock_button_set_popup_side (MOCKA_DOCK_BUTTON (button), GPOINTER_TO_INT (user_data));
}

/* The scroller's adjustment along the dock's length. */
static GtkAdjustment *
get_adjustment (MockaDockApplet *self)
{
  GtkScrolledWindow *scroller = GTK_SCROLLED_WINDOW (self->scroller);

  return gtk_orientable_get_orientation (GTK_ORIENTABLE (self->box)) == GTK_ORIENTATION_HORIZONTAL
             ? gtk_scrolled_window_get_hadjustment (scroller)
             : gtk_scrolled_window_get_vadjustment (scroller);
}

static void
apply_orient (MockaDockApplet *self, MatePanelAppletOrient orient)
{
  GtkOrientation orientation = orientation_for_orient (orient);
  gboolean horizontal = orientation == GTK_ORIENTATION_HORIZONTAL;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->box), orientation);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->outer), orientation);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scroller),
                                  horizontal ? GTK_POLICY_EXTERNAL : GTK_POLICY_NEVER,
                                  horizontal ? GTK_POLICY_NEVER : GTK_POLICY_EXTERNAL);
  gtk_button_set_image (
      GTK_BUTTON (self->arrow_start),
      gtk_image_new_from_icon_name (horizontal ? "pan-start-symbolic" : "pan-up-symbolic", GTK_ICON_SIZE_MENU));
  gtk_button_set_image (
      GTK_BUTTON (self->arrow_end),
      gtk_image_new_from_icon_name (horizontal ? "pan-end-symbolic" : "pan-down-symbolic", GTK_ICON_SIZE_MENU));
  self->popup_side = popup_side_for_orient (orient);
  mocka_thumbnails_set_side (self->thumbnails, self->popup_side);
  gtk_container_foreach (GTK_CONTAINER (self->box), set_button_popup_side, GINT_TO_POINTER (self->popup_side));
  update_size_hints (self);
}

static void
mocka_dock_applet_change_orient (MatePanelApplet *applet, MatePanelAppletOrient orient)
{
  apply_orient (MOCKA_DOCK_APPLET (applet), orient);
}

static void
set_button_size (GtkWidget *button, gpointer user_data)
{
  mocka_dock_button_set_size (MOCKA_DOCK_BUTTON (button), GPOINTER_TO_INT (user_data));
}

/* Buttons are square, as long on each side as the panel is thick. */
static void
mocka_dock_applet_change_size (MatePanelApplet *applet, guint size)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (applet);

  self->size = (gint)size;
  gtk_container_foreach (GTK_CONTAINER (self->box), set_button_size, GINT_TO_POINTER (self->size));
  update_size_hints (self);
}

/*
 * Launches through the tracker, which remembers the startup ID. action and
 * uris come from the app menu (SPEC section 9.1).
 */
static void
on_button_launch (MockaDockButton *button, const gchar *action, const gchar *const *uris, gpointer user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  MockaAppEntry *entry = mocka_dock_app_get_entry (mocka_dock_button_get_app (button));
  g_autoptr (GError) error = NULL;

  if (!mocka_window_tracker_launch (self->tracker, entry, action, uris, gtk_widget_get_display (GTK_WIDGET (button)),
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
pin_at (MockaDockApplet *self, const gchar *id, guint gap)
{
  g_auto (GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");
  g_auto (GStrv) pinned = mocka_pinned_list_insert ((const gchar *const *)ids, id, gap);

  g_settings_set_strv (self->shared_settings, "pinned-apps", (const gchar *const *)pinned);
}

/* Pin to dock: added after the apps already pinned (SPEC section 5). */
static void
on_button_pin (MockaDockButton *button, gpointer user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  MockaAppEntry *entry = mocka_dock_app_get_entry (mocka_dock_button_get_app (button));
  g_auto (GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");

  close_undo_popup (self);
  if (!g_strv_contains ((const gchar *const *)ids, entry->id))
    pin_at (self, entry->id, G_MAXUINT);
}

/* Undo: pins the app again at its previous position (SPEC section 10). */
static void
on_undo (MockaUndoPopup *popup, gpointer user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  const gchar *id = g_object_get_data (G_OBJECT (popup), "desktop-id");
  guint position = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (popup), "position"));
  g_auto (GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");

  if (!g_strv_contains ((const gchar *const *)ids, id))
    pin_at (self, id, position);
}

/*
 * Unpin from dock, then the "<App> unpinned" popup with Undo where the
 * button was (SPEC section 10).
 */
static void
on_button_unpin (MockaDockButton *button, gpointer user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  MockaAppEntry *entry = mocka_dock_app_get_entry (mocka_dock_button_get_app (button));
  g_auto (GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");
  g_auto (GStrv) pinned = NULL;
  g_autofree gchar *id = g_strdup (entry->id);
  g_autofree gchar *name = g_strdup (entry->name != NULL ? entry->name : entry->id);
  GdkRectangle rect;
  gboolean have_rect;
  guint position;

  close_undo_popup (self);

  /* The button goes away with the setting change when the app is not running. */
  have_rect = mocka_dock_button_get_screen_rect (button, &rect);

  pinned = mocka_pinned_list_remove ((const gchar *const *)ids, id, &position);
  g_settings_set_strv (self->shared_settings, "pinned-apps", (const gchar *const *)pinned);

  if (!have_rect)
    return;

  self->undo_popup = mocka_undo_popup_new (name);
  g_object_add_weak_pointer (G_OBJECT (self->undo_popup), (gpointer *)&self->undo_popup);
  g_object_set_data_full (G_OBJECT (self->undo_popup), "desktop-id", g_steal_pointer (&id), g_free);
  g_object_set_data (G_OBJECT (self->undo_popup), "position", GUINT_TO_POINTER (position));
  g_signal_connect (self->undo_popup, "undo", G_CALLBACK (on_undo), self);
  mocka_undo_popup_show_at (MOCKA_UNDO_POPUP (self->undo_popup), &rect, self->popup_side);
}

/*
 * A click on the dock or a focus change counts as clicking elsewhere. A
 * click also cancels a pending thumbnail popup (SPEC section 8), and hides
 * one that shows, except for a plain left click on an app with several
 * windows, which toggles it.
 */
static gboolean
on_dock_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  MockaDockApp *app = mocka_dock_button_get_app (MOCKA_DOCK_BUTTON (widget));
  GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask ();

  close_undo_popup (self);

  if (event->button == GDK_BUTTON_PRIMARY && mods == 0 && mocka_dock_app_get_windows (app)->len > 1)
    mocka_thumbnails_cancel (self->thumbnails);
  else
    mocka_thumbnails_hide (self->thumbnails);

  return FALSE;
}

static gboolean
on_dock_button_enter (GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  mocka_thumbnails_enter (MOCKA_DOCK_APPLET (user_data)->thumbnails, MOCKA_DOCK_BUTTON (widget));
  return FALSE;
}

static gboolean
on_dock_button_leave (GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  mocka_thumbnails_leave (MOCKA_DOCK_APPLET (user_data)->thumbnails);
  return FALSE;
}

static void
on_toggle_thumbnails (MockaDockButton *button, gpointer user_data)
{
  mocka_thumbnails_toggle (MOCKA_DOCK_APPLET (user_data)->thumbnails, button);
}

static void
on_active_window_changed (WnckScreen *screen, WnckWindow *previous, gpointer user_data)
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
  { (gchar *)MOCKA_DOCK_APP_TARGET, GTK_TARGET_SAME_APP, DROP_TARGET_APP },
  { (gchar *)"text/uri-list", 0, DROP_TARGET_URI_LIST },
};

/* Pinned apps come first in the dock, so they are its first buttons. */
static guint
count_pinned (MockaDockApplet *self)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->model));
  guint i;

  for (i = 0; i < n; i++)
    {
      g_autoptr (MockaDockApp) app = g_list_model_get_item (G_LIST_MODEL (self->model), i);

      if (!mocka_dock_app_get_pinned (app))
        break;
    }

  return i;
}

/* The coordinate along the dock's length. */
static gdouble
along (MockaDockApplet *self, gdouble x, gdouble y)
{
  return gtk_orientable_get_orientation (GTK_ORIENTABLE (self->box)) == GTK_ORIENTATION_HORIZONTAL ? x : y;
}

/*
 * The gap of the pinned apps under the pointer, at x, y in the applet: 0
 * before the first, up to the number of pinned apps after the last.
 * in_running_area is set when the pointer is over the apps that are not
 * pinned.
 */
static guint
drop_gap_at (MockaDockApplet *self, gint x, gint y, gboolean *in_running_area)
{
  g_autoptr (GList) children = gtk_container_get_children (GTK_CONTAINER (self->box));
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
          < along (self, gtk_widget_get_allocated_width (child), gtk_widget_get_allocated_height (child)) / 2.0)
        return i;
    }

  return pinned;
}

static void
set_drop_marker (MockaDockApplet *self, gboolean show)
{
  self->show_drop_marker = show;
  gtk_widget_queue_draw (self->box);
}

/* A line where the app will land, in the theme's highlight color. */
static gboolean
on_box_draw (GtkWidget *box, cairo_t *cr, gpointer user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  g_autoptr (GList) children = NULL;
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
  horizontal = gtk_orientable_get_orientation (GTK_ORIENTABLE (box)) == GTK_ORIENTATION_HORIZONTAL;

  if (child != NULL)
    {
      gtk_widget_get_allocation (child, &allocation);
      if (horizontal)
        position = allocation.x - box_allocation.x + (after ? allocation.width : 0);
      else
        position = allocation.y - box_allocation.y + (after ? allocation.height : 0);
    }

  if (horizontal)
    cairo_rectangle (cr, CLAMP (position - 1, 0, box_allocation.width - 2), 0, 2, box_allocation.height);
  else
    cairo_rectangle (cr, 0, CLAMP (position - 1, 0, box_allocation.height - 2), box_allocation.width, 2);
  cairo_fill (cr);

  return FALSE;
}

/*
 * While dragging: dock buttons and desktop entry files are accepted. A
 * running app that is not pinned only pins when dropped among the pinned
 * apps.
 */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) the signal fixes this signature */
static gboolean
on_drag_motion (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data)
/* NOLINTEND(bugprone-easily-swappable-parameters) */
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  GtkWidget *source = gtk_drag_get_source_widget (context);
  gboolean in_running_area;

  if (gtk_drag_dest_find_target (widget, context, NULL) == GDK_NONE)
    {
      /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) GdkDragAction has no value for refusing a drop */
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
  gdk_drag_status (context, source != NULL ? GDK_ACTION_MOVE : GDK_ACTION_COPY, time);
  return TRUE;
}

/* Also emitted right before a drop, so drop_gap is kept. */
static void
on_drag_leave (GtkWidget *widget, GdkDragContext *context, guint time, gpointer user_data)
{
  set_drop_marker (MOCKA_DOCK_APPLET (user_data), FALSE);
}

/* NOLINTBEGIN(bugprone-easily-swappable-parameters) the signal fixes this signature */
static gboolean
on_drag_drop (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data)
/* NOLINTEND(bugprone-easily-swappable-parameters) */
{
  GdkAtom target = gtk_drag_dest_find_target (widget, context, NULL);

  if (target == GDK_NONE)
    return FALSE;

  gtk_drag_get_data (widget, context, target, time);
  return TRUE;
}

/*
 * The gap of the pinned-apps setting matching a gap between the pinned
 * buttons. They differ when the setting holds apps that are not installed,
 * which have no button: the gap is placed right before the ID of the button
 * after it, or at the end of the setting after the last button.
 */
static guint
setting_gap (MockaDockApplet *self, guint button_gap)
{
  g_auto (GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");
  g_autoptr (MockaDockApp) app = NULL;
  MockaAppEntry *entry;
  guint i;

  if (button_gap >= count_pinned (self))
    return G_MAXUINT;

  app = g_list_model_get_item (G_LIST_MODEL (self->model), button_gap);
  entry = mocka_dock_app_get_entry (app);

  for (i = 0; ids[i] != NULL; i++)
    if (g_str_equal (ids[i], entry->id))
      return i;

  return G_MAXUINT;
}

/*
 * Pins each dropped desktop entry file, in order, from the drop gap on.
 * Files outside the applications directories are first copied into the
 * user's own. Returns the number pinned.
 */
static guint
pin_dropped_files (MockaDockApplet *self, gchar **uris)
{
  const gchar *const *data_dirs = g_get_system_data_dirs ();
  g_autofree gchar *user_dir = g_build_filename (g_get_user_data_dir (), "applications", NULL);
  g_autoptr (GPtrArray) app_dirs = g_ptr_array_new_with_free_func (g_free);
  g_autoptr (GPtrArray) ids = g_ptr_array_new_with_free_func (g_free);
  guint gap;
  guint i;

  g_ptr_array_add (app_dirs, g_strdup (user_dir));
  for (i = 0; data_dirs[i] != NULL; i++)
    g_ptr_array_add (app_dirs, g_build_filename (data_dirs[i], "applications", NULL));
  g_ptr_array_add (app_dirs, NULL);

  for (i = 0; uris != NULL && uris[i] != NULL; i++)
    {
      g_autofree gchar *path = g_filename_from_uri (uris[i], NULL, NULL);
      g_autoptr (GError) error = NULL;
      gchar *id;

      if (path == NULL)
        continue;

      id = mocka_desktop_import (path, (const gchar *const *)app_dirs->pdata, user_dir, &error);
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

  gap = setting_gap (self, self->drop_gap);
  for (i = 0; i < ids->len; i++)
    pin_at (self, g_ptr_array_index (ids, i), gap == G_MAXUINT ? gap : gap + i);

  return ids->len;
}

/* NOLINTBEGIN(bugprone-easily-swappable-parameters) the signal fixes this signature */
static void
on_drag_data_received (GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data, guint info,
                       guint time, gpointer user_data)
/* NOLINTEND(bugprone-easily-swappable-parameters) */
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  gboolean success = FALSE;

  close_undo_popup (self);
  set_drop_marker (self, FALSE);

  if (info == DROP_TARGET_APP && gtk_selection_data_get_length (data) > 0)
    {
      g_autofree gchar *id
          = g_strndup ((const gchar *)gtk_selection_data_get_data (data), gtk_selection_data_get_length (data));

      pin_at (self, id, setting_gap (self, self->drop_gap));
      success = TRUE;
    }
  else if (info == DROP_TARGET_URI_LIST)
    {
      g_auto (GStrv) uris = gtk_selection_data_get_uris (data);

      success = pin_dropped_files (self, uris) > 0;
    }

  gtk_drag_finish (context, success, FALSE, time);
}

/* The button of an app the dock launched pulses while it starts. */
static void
on_launch_state_changed (MockaWindowTracker *tracker, const gchar *desktop_id, gboolean launching, gpointer user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  g_autoptr (GList) children = gtk_container_get_children (GTK_CONTAINER (self->box));
  GList *l;

  for (l = children; l != NULL; l = l->next)
    {
      MockaDockButton *button = MOCKA_DOCK_BUTTON (l->data);

      if (g_str_equal (mocka_dock_app_get_key (mocka_dock_button_get_app (button)), desktop_id))
        mocka_dock_button_set_launching (button, launching);
    }
}

/* Keeps the buttons in the same order as the apps in the model. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) the signal fixes this signature */
static void
on_items_changed (GListModel *list, guint position, guint removed, guint added, gpointer user_data)
/* NOLINTEND(bugprone-easily-swappable-parameters) */
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
      g_autoptr (MockaDockApp) app = g_list_model_get_item (list, position + i);
      GtkWidget *button = mocka_dock_button_new (app);

      mocka_dock_button_set_size (MOCKA_DOCK_BUTTON (button), self->size);
      mocka_dock_button_set_popup_side (MOCKA_DOCK_BUTTON (button), self->popup_side);
      /* The tracker adds the open windows while it is being created. */
      if (self->tracker != NULL)
        mocka_dock_button_set_launching (MOCKA_DOCK_BUTTON (button), mocka_window_tracker_is_launching (
                                                                         self->tracker, mocka_dock_app_get_key (app)));
      g_signal_connect (button, "launch", G_CALLBACK (on_button_launch), self);
      g_signal_connect (button, "pin", G_CALLBACK (on_button_pin), self);
      g_signal_connect (button, "unpin", G_CALLBACK (on_button_unpin), self);
      g_signal_connect (button, "button-press-event", G_CALLBACK (on_dock_button_press), self);
      g_signal_connect (button, "enter-notify-event", G_CALLBACK (on_dock_button_enter), self);
      g_signal_connect (button, "leave-notify-event", G_CALLBACK (on_dock_button_leave), self);
      g_signal_connect (button, "toggle-thumbnails", G_CALLBACK (on_toggle_thumbnails), self);
      g_signal_connect_object (button, "drag-begin", G_CALLBACK (mocka_thumbnails_hide), self->thumbnails,
                               G_CONNECT_SWAPPED);
      gtk_box_pack_start (GTK_BOX (self->box), button, FALSE, FALSE, 0);
      gtk_box_reorder_child (GTK_BOX (self->box), button, (gint)(position + i));
      gtk_widget_show_all (button);
    }

  update_size_hints (self);
}

/*
 * Overflow arrows (SPEC section 3): shown only while the buttons do not
 * fit, each disabled once the buttons are scrolled to its end.
 */
static void
update_arrows (MockaDockApplet *self)
{
  GtkAdjustment *adjustment = get_adjustment (self);
  gdouble lower = gtk_adjustment_get_lower (adjustment);
  gdouble upper = gtk_adjustment_get_upper (adjustment);
  gdouble page = gtk_adjustment_get_page_size (adjustment);
  gdouble value = gtk_adjustment_get_value (adjustment);
  gboolean shown = gtk_widget_get_visible (self->arrow_start);
  gdouble room = page;

  /* The arrows take room of their own, which the buttons get back without them. */
  if (shown)
    room += along (self, gtk_widget_get_allocated_width (self->arrow_start),
                   gtk_widget_get_allocated_height (self->arrow_start))
            + along (self, gtk_widget_get_allocated_width (self->arrow_end),
                     gtk_widget_get_allocated_height (self->arrow_end));

  gtk_widget_set_visible (self->arrow_start, upper - lower > room + 0.5);
  gtk_widget_set_visible (self->arrow_end, upper - lower > room + 0.5);
  gtk_widget_set_sensitive (self->arrow_start, value > lower + 0.5);
  gtk_widget_set_sensitive (self->arrow_end, value < upper - page - 0.5);
}

/* Each click scrolls by one button, to the next button boundary. */
static void
scroll_by_button (MockaDockApplet *self, gint direction)
{
  GtkAdjustment *adjustment = get_adjustment (self);
  gdouble value = gtk_adjustment_get_value (adjustment);
  gdouble step = MAX (self->size, 1);

  if (direction > 0)
    value = (floor (value / step + 0.01) + 1) * step;
  else
    value = (ceil (value / step - 0.01) - 1) * step;

  gtk_adjustment_set_value (adjustment, value);
}

static void
on_arrow_start_clicked (GtkButton *button, gpointer user_data)
{
  scroll_by_button (MOCKA_DOCK_APPLET (user_data), -1);
}

static void
on_arrow_end_clicked (GtkButton *button, gpointer user_data)
{
  scroll_by_button (MOCKA_DOCK_APPLET (user_data), 1);
}

/* The mouse wheel never scrolls the dock; it is kept for window cycling. */
static gboolean
on_scroller_scroll (GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
  return TRUE;
}

static GtkWidget *
arrow_new (MockaDockApplet *self, GCallback callback)
{
  GtkWidget *arrow = gtk_button_new ();

  gtk_button_set_relief (GTK_BUTTON (arrow), GTK_RELIEF_NONE);
  gtk_widget_set_can_focus (arrow, FALSE);
  gtk_widget_set_no_show_all (arrow, TRUE);
  gtk_style_context_add_class (gtk_widget_get_style_context (arrow), "mocka-dock-arrow");
  g_signal_connect (arrow, "clicked", callback, self);

  return arrow;
}

/* Room for one arrow along the dock: a menu icon with a little padding. */
#define ARROW_LENGTH 20

/* The length of the monitor the dock is on, along the dock. */
static gint
monitor_length (MockaDockApplet *self)
{
  GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (self));
  GdkWindow *window = gtk_widget_get_window (GTK_WIDGET (self));
  GdkMonitor *monitor = NULL;
  GdkRectangle geometry;

  if (window != NULL)
    monitor = gdk_display_get_monitor_at_window (display, window);
  if (monitor == NULL)
    monitor = gdk_display_get_primary_monitor (display);
  if (monitor == NULL)
    monitor = gdk_display_get_monitor (display, 0);
  if (monitor == NULL)
    return 0;

  gdk_monitor_get_geometry (monitor, &geometry);
  return gtk_orientable_get_orientation (GTK_ORIENTABLE (self->box)) == GTK_ORIENTATION_HORIZONTAL ? geometry.width
                                                                                                   : geometry.height;
}

/*
 * Tells the panel which lengths suit the dock (SPEC section 3). On an
 * expanded panel the dock takes all the free room, up to the monitor's
 * length. Otherwise it asks for the length of its buttons, since the panel
 * grows to whatever its applets ask for. Either way it can shrink to one
 * button between the two arrows when the panel has no room left.
 */
static void
update_size_hints (MockaDockApplet *self)
{
  g_autoptr (GList) buttons = gtk_container_get_children (GTK_CONTAINER (self->box));
  gint size = MAX (self->size, 1);

  /*
   * Buttons are squares of the panel's size. Worked out rather than asked
   * of GTK, which gives no size for widgets that are not shown yet.
   */
  gint wanted = (gint)g_list_length (buttons) * size;
  gint least = size + 2 * ARROW_LENGTH;

  if (self->toplevel_settings != NULL && g_settings_get_boolean (self->toplevel_settings, "expand"))
    wanted = MAX (wanted, monitor_length (self));

  /* The panel keeps a pointer to the hints, not a copy. */
  self->size_hints[0] = MAX (wanted, 1);
  self->size_hints[1] = MIN (least, self->size_hints[0]);
  mate_panel_applet_set_size_hints (MATE_PANEL_APPLET (self), self->size_hints, G_N_ELEMENTS (self->size_hints), 0);
}

/*
 * Follows the settings of the panel the dock is on, which change when the
 * panel is expanded or the dock is moved to another panel. They belong to
 * mate-panel: the dock's object is the parent of its preferences path.
 */
static void
watch_panel (MockaDockApplet *self)
{
  g_autofree gchar *prefs_path = mate_panel_applet_get_preferences_path (MATE_PANEL_APPLET (self));
  g_autofree gchar *object_path = NULL;
  g_autofree gchar *toplevel_id = NULL;
  g_autofree gchar *toplevel_path = NULL;
  gsize length;

  g_clear_object (&self->toplevel_settings);

  if (self->object_settings == NULL)
    {
      if (prefs_path == NULL || !g_str_has_suffix (prefs_path, "/prefs/"))
        return;

      length = strlen (prefs_path) - strlen ("prefs/");
      object_path = g_strndup (prefs_path, length);
      self->object_settings = g_settings_new_with_path ("org.mate.panel.object", object_path);
      g_signal_connect_swapped (self->object_settings, "changed::toplevel-id", G_CALLBACK (watch_panel), self);
    }

  toplevel_id = g_settings_get_string (self->object_settings, "toplevel-id");
  if (toplevel_id[0] != '\0')
    {
      toplevel_path = g_strdup_printf ("/org/mate/panel/toplevels/%s/", toplevel_id);
      self->toplevel_settings = g_settings_new_with_path ("org.mate.panel.toplevel", toplevel_path);
      g_signal_connect_swapped (self->toplevel_settings, "changed::expand", G_CALLBACK (update_size_hints), self);
    }

  update_size_hints (self);
}

static void
load_style (void)
{
  g_autoptr (GtkCssProvider) provider = gtk_css_provider_new ();

  /* Let the square size set the button size, not the theme's padding. */
  gtk_css_provider_load_from_data (provider,
                                   ".mocka-dock-button { padding: 0; min-width: 0; min-height: 0; }"
                                   ".mocka-dock-arrow { padding: 0 2px; min-width: 0; min-height: 0; }"
                                   /* The panel draws the background behind the scrolled buttons. */
                                   ".mocka-dock-scroller, .mocka-dock-scroller viewport"
                                   " { background: none; border: none; box-shadow: none; }",
                                   -1, NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/*
 * Pinned apps from the shared pinned-apps setting (SPEC section 10). IDs of
 * apps that are not installed are kept in the setting but not shown.
 */
static void
update_pinned (MockaDockApplet *self)
{
  g_auto (GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");
  g_autoptr (GPtrArray) entries = g_ptr_array_new ();
  guint i;

  for (i = 0; ids[i] != NULL; i++)
    {
      MockaAppEntry *entry = mocka_app_index_lookup (self->index, ids[i]);

      if (entry != NULL)
        g_ptr_array_add (entries, entry);
    }

  mocka_dock_model_set_pinned (self->model, entries);
}

/* About, from the dock menu (SPEC section 9.3). */
static void
on_about (GtkAction *action, gpointer user_data)
{
  const gchar *authors[] = { "The Mocka Desktop Project", NULL };

  gtk_show_about_dialog (NULL, "program-name", _ ("Mocka Dock"), "version", PACKAGE_VERSION, "comments",
                         _ ("Pinned and running applications"), "logo-icon-name", "user-desktop", "copyright",
                         "Copyright \xc2\xa9 2026 The Mocka Desktop Project", "license-type", GTK_LICENSE_BSD_3,
                         "authors", authors, "website", "https://github.com/mocka-desktop/mocka-dock", NULL);
}

/*
 * Dock menu (SPEC section 9.3): the panel's applet menu, shown on Ctrl +
 * right click anywhere on the dock and right click on empty space, with
 * the dock's items above the panel's own. Preferences is added once its
 * window exists (M7).
 */
static void
setup_dock_menu (MockaDockApplet *self)
{
  static const gchar menu_xml[] = "<menuitem name=\"About\" action=\"About\" />";
  GtkActionGroup *group;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  static const GtkActionEntry entries[] = {
    { "About", "help-about", N_ ("_About"), NULL, NULL, G_CALLBACK (on_about) },
  };

  group = gtk_action_group_new ("MockaDockActions");
  gtk_action_group_set_translation_domain (group, GETTEXT_PACKAGE);
  gtk_action_group_add_actions (group, entries, G_N_ELEMENTS (entries), self);
  G_GNUC_END_IGNORE_DEPRECATIONS

  mate_panel_applet_setup_menu (MATE_PANEL_APPLET (self), menu_xml, group);
  g_object_unref (group);
}

static void
mocka_dock_applet_dispose (GObject *object)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (object);

  close_undo_popup (self);
  g_clear_object (&self->thumbnails);
  g_clear_object (&self->toplevel_settings);
  g_clear_object (&self->object_settings);
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

  self->outer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add (GTK_CONTAINER (self), self->outer);

  self->arrow_start = arrow_new (self, G_CALLBACK (on_arrow_start_clicked));
  gtk_box_pack_start (GTK_BOX (self->outer), self->arrow_start, FALSE, FALSE, 0);

  self->scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (self->scroller), GTK_SHADOW_NONE);
  /* Ask for the length of all buttons, while accepting less. */
  gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (self->scroller), TRUE);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (self->scroller), TRUE);
  gtk_style_context_add_class (gtk_widget_get_style_context (self->scroller), "mocka-dock-scroller");
  g_signal_connect (self->scroller, "scroll-event", G_CALLBACK (on_scroller_scroll), NULL);
  gtk_box_pack_start (GTK_BOX (self->outer), self->scroller, TRUE, TRUE, 0);

  self->arrow_end = arrow_new (self, G_CALLBACK (on_arrow_end_clicked));
  gtk_box_pack_start (GTK_BOX (self->outer), self->arrow_end, FALSE, FALSE, 0);

  self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add (GTK_CONTAINER (self->scroller), self->box);

  self->thumbnails = mocka_thumbnails_new (GTK_WIDGET (self));
  gtk_viewport_set_shadow_type (GTK_VIEWPORT (gtk_bin_get_child (GTK_BIN (self->scroller))), GTK_SHADOW_NONE);

  g_signal_connect_swapped (gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->scroller)), "changed",
                            G_CALLBACK (update_arrows), self);
  g_signal_connect_swapped (gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->scroller)), "value-changed",
                            G_CALLBACK (update_arrows), self);
  g_signal_connect_swapped (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroller)), "changed",
                            G_CALLBACK (update_arrows), self);
  g_signal_connect_swapped (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroller)), "value-changed",
                            G_CALLBACK (update_arrows), self);
}

static void
mocka_dock_applet_setup (MockaDockApplet *self)
{
  MatePanelApplet *applet = MATE_PANEL_APPLET (self);

  load_style ();

  /* The dock takes the length the panel has free (SPEC section 3). */
  mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MAJOR | MATE_PANEL_APPLET_EXPAND_MINOR);
  mate_panel_applet_set_background_widget (applet, GTK_WIDGET (self));

  self->size = (gint)mate_panel_applet_get_size (applet);
  apply_orient (self, mate_panel_applet_get_orient (applet));

  self->index = mocka_app_index_new_for_system ();
  self->model = mocka_dock_model_new ();
  g_signal_connect (self->model, "items-changed", G_CALLBACK (on_items_changed), self);
  self->tracker = mocka_window_tracker_new (self->wnck, self->index, self->model);
  g_signal_connect (self->tracker, "launch-state-changed", G_CALLBACK (on_launch_state_changed), self);

  self->settings = mate_panel_applet_settings_new (applet, (gchar *)"org.mocka_desktop.Dock.Instance");
  g_settings_bind (self->settings, "show-all-workspaces", self->tracker, "show-all-workspaces", G_SETTINGS_BIND_GET);

  self->shared_settings = g_settings_new ("org.mocka_desktop.Dock");
  g_signal_connect_swapped (self->shared_settings, "changed::pinned-apps", G_CALLBACK (update_pinned), self);
  g_signal_connect_swapped (self->index, "changed", G_CALLBACK (update_pinned), self);
  update_pinned (self);

  /* The focused app's button is highlighted (SPEC section 12). */
  g_signal_connect_object (wnck_handle_get_default_screen (self->wnck), "active-window-changed",
                           G_CALLBACK (gtk_widget_queue_draw), self->box, G_CONNECT_SWAPPED);
  g_signal_connect_object (wnck_handle_get_default_screen (self->wnck), "active-window-changed",
                           G_CALLBACK (on_active_window_changed), self, 0);

  gtk_drag_dest_set (GTK_WIDGET (self), 0, drop_targets, G_N_ELEMENTS (drop_targets),
                     GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (self, "drag-motion", G_CALLBACK (on_drag_motion), self);
  g_signal_connect (self, "drag-leave", G_CALLBACK (on_drag_leave), self);
  g_signal_connect (self, "drag-drop", G_CALLBACK (on_drag_drop), self);
  g_signal_connect (self, "drag-data-received", G_CALLBACK (on_drag_data_received), self);
  g_signal_connect_after (self->box, "draw", G_CALLBACK (on_box_draw), self);

  setup_dock_menu (self);
  watch_panel (self);
  /* The monitor is only known once the dock has a window. */
  g_signal_connect_swapped (self, "realize", G_CALLBACK (update_size_hints), self);

  gtk_widget_show_all (GTK_WIDGET (self));
}

static gboolean
mocka_dock_applet_factory (MatePanelApplet *applet, const gchar *iid, gpointer user_data)
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
MATE_PANEL_APPLET_IN_PROCESS_FACTORY (MOCKA_DOCK_FACTORY_ID, MOCKA_TYPE_DOCK_APPLET, "Mocka Dock",
                                      mocka_dock_applet_factory, NULL)