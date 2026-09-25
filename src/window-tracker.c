/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Follows the windows on the screen through libwnck and puts each one under
 * its app in the dock model. A window is matched again when its class
 * changes and when installed applications change (SPEC section 6), and it
 * leaves the dock while it asks to skip the taskbar (SPEC section 5).
 *
 * Unless show-all-workspaces is set, windows on other workspaces are hidden
 * (SPEC section 5, current workspace mode).
 */

#include "config.h"

#include "window-tracker.h"

#include <gio/gdesktopappinfo.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>

#include "matcher.h"

struct _MockaWindowTracker
{
  GObject parent_instance;

  WnckScreen *screen;
  MockaAppIndex *index;
  MockaDockModel *model;
  gboolean show_all_workspaces;
  GHashTable *launches;  /* startup ID → desktop entry ID, apps the dock launched */
};

enum
{
  PROP_0,
  PROP_SHOW_ALL_WORKSPACES,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (MockaWindowTracker, mocka_window_tracker, G_TYPE_OBJECT)

/* On the current workspace, on all workspaces (sticky), or all are shown. */
static gboolean
is_shown (MockaWindowTracker *self,
          WnckWindow         *window)
{
  WnckWorkspace *workspace;

  if (self->show_all_workspaces)
    return TRUE;

  workspace = wnck_screen_get_active_workspace (self->screen);
  return workspace == NULL || wnck_window_is_on_workspace (window, workspace);
}

static void
update_visibility (MockaWindowTracker *self)
{
  GList *l;

  for (l = wnck_screen_get_windows (self->screen); l != NULL; l = l->next)
    mocka_dock_model_set_window_visible (self->model, l->data,
                                         is_shown (self, l->data));
}

/* The window's own _NET_STARTUP_ID, or NULL. */
static gchar *
read_window_startup_id (WnckWindow *window)
{
  GdkDisplay *display = gdk_display_get_default ();
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  Atom type;
  int format;
  unsigned long n_items, bytes_after;
  unsigned char *data = NULL;
  gchar *id = NULL;

  gdk_x11_display_error_trap_push (display);
  if (XGetWindowProperty (dpy, wnck_window_get_xid (window),
                          gdk_x11_get_xatom_by_name_for_display (display,
                                                                 "_NET_STARTUP_ID"),
                          0, 1024, False, AnyPropertyType, &type, &format,
                          &n_items, &bytes_after, &data) == Success
      && data != NULL && format == 8 && n_items > 0)
    id = g_strndup ((const gchar *) data, n_items);
  if (data != NULL)
    XFree (data);
  gdk_x11_display_error_trap_pop_ignored (display);

  return id;
}

/*
 * The startup notification ID from the window, or from its client leader,
 * where GTK apps set it (SPEC section 6, step 5).
 */
static gchar *
get_startup_id (WnckWindow *window)
{
  WnckApplication *application;
  gchar *id = read_window_startup_id (window);

  if (id != NULL)
    return id;

  application = wnck_window_get_application (window);
  if (application != NULL && wnck_application_get_startup_id (application) != NULL)
    return g_strdup (wnck_application_get_startup_id (application));

  return NULL;
}

static void
update_window (MockaWindowTracker *self,
               WnckWindow         *window)
{
  g_autoptr(MockaAppEntry) entry = NULL;
  g_autofree gchar *key = NULL;
  g_autofree gchar *startup_id = NULL;
  const gchar *res_class;

  if (wnck_window_is_skip_tasklist (window))
    {
      mocka_dock_model_remove_window (self->model, window);
      return;
    }

  /* Only needed for windows of apps the dock launched. */
  if (g_hash_table_size (self->launches) > 0)
    startup_id = get_startup_id (window);

  res_class = wnck_window_get_class_group_name (window);
  entry = mocka_matcher_match (self->index,
                               wnck_window_get_class_instance_name (window),
                               res_class, startup_id, self->launches, NULL);
  key = mocka_dock_app_key_for (entry, res_class);

  mocka_dock_model_add_window (self->model, window, key, entry,
                               is_shown (self, window));
}

static void
on_workspace_changed (WnckWindow *window,
                      gpointer    user_data)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (user_data);

  mocka_dock_model_set_window_visible (self->model, window,
                                       is_shown (self, window));
}

static void
on_active_workspace_changed (WnckScreen    *screen,
                             WnckWorkspace *previous,
                             gpointer       user_data)
{
  update_visibility (MOCKA_WINDOW_TRACKER (user_data));
}

static void
on_class_changed (WnckWindow *window,
                  gpointer    user_data)
{
  update_window (MOCKA_WINDOW_TRACKER (user_data), window);
}

static void
on_state_changed (WnckWindow      *window,
                  WnckWindowState  changed_mask,
                  WnckWindowState  new_state,
                  gpointer         user_data)
{
  if (changed_mask & WNCK_WINDOW_STATE_SKIP_TASKLIST)
    update_window (MOCKA_WINDOW_TRACKER (user_data), window);
}

static void
track_window (MockaWindowTracker *self,
              WnckWindow         *window)
{
  g_signal_connect_object (window, "class-changed",
                           G_CALLBACK (on_class_changed), self, 0);
  g_signal_connect_object (window, "state-changed",
                           G_CALLBACK (on_state_changed), self, 0);
  g_signal_connect_object (window, "workspace-changed",
                           G_CALLBACK (on_workspace_changed), self, 0);
  update_window (self, window);
}

static void
on_window_opened (WnckScreen *screen,
                  WnckWindow *window,
                  gpointer    user_data)
{
  track_window (MOCKA_WINDOW_TRACKER (user_data), window);
}

static void
on_window_closed (WnckScreen *screen,
                  WnckWindow *window,
                  gpointer    user_data)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (user_data);

  g_signal_handlers_disconnect_by_data (window, self);
  mocka_dock_model_remove_window (self->model, window);
}

static void
on_index_changed (MockaAppIndex *index,
                  gpointer       user_data)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (user_data);
  GList *l;

  for (l = wnck_screen_get_windows (self->screen); l != NULL; l = l->next)
    update_window (self, l->data);
}

static void
mocka_window_tracker_dispose (GObject *object)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (object);
  GList *l;

  if (self->screen != NULL)
    {
      for (l = wnck_screen_get_windows (self->screen); l != NULL; l = l->next)
        g_signal_handlers_disconnect_by_data (l->data, self);
      g_signal_handlers_disconnect_by_data (self->screen, self);
      self->screen = NULL;
    }

  if (self->index != NULL)
    g_signal_handlers_disconnect_by_data (self->index, self);
  g_clear_object (&self->index);
  g_clear_object (&self->model);

  G_OBJECT_CLASS (mocka_window_tracker_parent_class)->dispose (object);
}

static void
mocka_window_tracker_finalize (GObject *object)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (object);

  g_hash_table_unref (self->launches);

  G_OBJECT_CLASS (mocka_window_tracker_parent_class)->finalize (object);
}

static void
mocka_window_tracker_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (object);

  switch (prop_id)
    {
    case PROP_SHOW_ALL_WORKSPACES:
      g_value_set_boolean (value, self->show_all_workspaces);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
mocka_window_tracker_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (object);

  switch (prop_id)
    {
    case PROP_SHOW_ALL_WORKSPACES:
      if (self->show_all_workspaces == g_value_get_boolean (value))
        return;
      self->show_all_workspaces = g_value_get_boolean (value);
      if (self->screen != NULL)
        update_visibility (self);
      g_object_notify_by_pspec (object, pspec);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
mocka_window_tracker_class_init (MockaWindowTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = mocka_window_tracker_dispose;
  object_class->finalize = mocka_window_tracker_finalize;
  object_class->get_property = mocka_window_tracker_get_property;
  object_class->set_property = mocka_window_tracker_set_property;

  /* Setting show-all-workspaces (SPEC section 16). */
  properties[PROP_SHOW_ALL_WORKSPACES] =
    g_param_spec_boolean ("show-all-workspaces", NULL, NULL, FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
mocka_window_tracker_init (MockaWindowTracker *self)
{
  /* Kept for the session: apps keep their startup ID on later windows. */
  self->launches = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, g_free);
}

static void
on_launched (GAppLaunchContext *context,
             GAppInfo          *info,
             GVariant          *platform_data,
             gpointer           user_data)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (user_data);
  const gchar *desktop_id = g_object_get_data (G_OBJECT (context),
                                               "mocka-desktop-id");
  const gchar *startup_id = NULL;

  if (g_variant_lookup (platform_data, "startup-notification-id", "&s",
                        &startup_id)
      && startup_id != NULL && *startup_id != '\0')
    g_hash_table_insert (self->launches, g_strdup (startup_id),
                         g_strdup (desktop_id));
}

/*
 * Launches an app, with startup notification and the time of the click so
 * its window gets focus. The startup ID is remembered, so the app's windows
 * are matched to it even when their class matches no desktop entry (SPEC
 * section 6, step 5).
 */
gboolean
mocka_window_tracker_launch (MockaWindowTracker  *self,
                             MockaAppEntry       *entry,
                             GdkDisplay          *display,
                             guint32              timestamp,
                             GError             **error)
{
  g_autoptr(GDesktopAppInfo) info = NULL;
  g_autoptr(GdkAppLaunchContext) context = NULL;

  g_return_val_if_fail (MOCKA_IS_WINDOW_TRACKER (self), FALSE);
  g_return_val_if_fail (entry != NULL, FALSE);

  info = g_desktop_app_info_new_from_filename (entry->path);
  if (info == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Cannot read desktop entry %s", entry->path);
      return FALSE;
    }

  context = gdk_display_get_app_launch_context (display);
  gdk_app_launch_context_set_timestamp (context, timestamp);
  g_object_set_data_full (G_OBJECT (context), "mocka-desktop-id",
                          g_strdup (entry->id), g_free);
  g_signal_connect (context, "launched", G_CALLBACK (on_launched), self);

  return g_app_info_launch (G_APP_INFO (info), NULL,
                            G_APP_LAUNCH_CONTEXT (context), error);
}

/*
 * Starts tracking the windows of the default screen of wnck. The screen
 * belongs to wnck, which must outlive the tracker.
 */
MockaWindowTracker *
mocka_window_tracker_new (WnckHandle     *wnck,
                          MockaAppIndex  *index,
                          MockaDockModel *model)
{
  MockaWindowTracker *self;
  GList *l;

  g_return_val_if_fail (WNCK_IS_HANDLE (wnck), NULL);
  g_return_val_if_fail (MOCKA_IS_APP_INDEX (index), NULL);
  g_return_val_if_fail (MOCKA_IS_DOCK_MODEL (model), NULL);

  self = g_object_new (MOCKA_TYPE_WINDOW_TRACKER, NULL);
  self->screen = wnck_handle_get_default_screen (wnck);
  self->index = g_object_ref (index);
  self->model = g_object_ref (model);

  wnck_screen_force_update (self->screen);

  g_signal_connect (self->screen, "window-opened",
                    G_CALLBACK (on_window_opened), self);
  g_signal_connect (self->screen, "window-closed",
                    G_CALLBACK (on_window_closed), self);
  g_signal_connect (self->screen, "active-workspace-changed",
                    G_CALLBACK (on_active_workspace_changed), self);
  g_signal_connect (self->index, "changed",
                    G_CALLBACK (on_index_changed), self);

  /* Existing windows, in the order they were mapped. */
  for (l = wnck_screen_get_windows (self->screen); l != NULL; l = l->next)
    track_window (self, l->data);

  return self;
}