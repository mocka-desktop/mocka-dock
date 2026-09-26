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

#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include <gio/gdesktopappinfo.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>

#include "matcher.h"
#include "startup-message.h"

/* Seconds after which a launch counts as done anyway (SPEC section 7). */
#define LAUNCH_TIMEOUT_SECONDS 15

struct _MockaWindowTracker
{
  GObject parent_instance;

  WnckScreen *screen;
  MockaAppIndex *index;
  MockaDockModel *model;
  gboolean show_all_workspaces;
  GHashTable *launches;  /* startup ID → desktop entry ID, apps the dock launched */
  GHashTable *pending;   /* startup ID → PendingLaunch, apps still starting */
  GHashTable *messages;  /* sender XID → GString, startup messages in parts */
  GdkWindow *root;
};

/* An app the dock launched whose button pulses (SPEC section 7). */
typedef struct
{
  MockaWindowTracker *self;
  gchar *startup_id;
  gchar *desktop_id;
  guint timeout_id;
} PendingLaunch;

enum
{
  PROP_0,
  PROP_SHOW_ALL_WORKSPACES,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
  SIGNAL_LAUNCH_STATE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (MockaWindowTracker, mocka_window_tracker, G_TYPE_OBJECT)

static void
pending_launch_free (PendingLaunch *launch)
{
  g_clear_handle_id (&launch->timeout_id, g_source_remove);
  g_free (launch->startup_id);
  g_free (launch->desktop_id);
  g_free (launch);
}

/* An app is starting while one of its launches is pending. */
gboolean
mocka_window_tracker_is_launching (MockaWindowTracker *self,
                                   const gchar        *desktop_id)
{
  GHashTableIter iter;
  gpointer value;

  g_return_val_if_fail (MOCKA_IS_WINDOW_TRACKER (self), FALSE);

  g_hash_table_iter_init (&iter, self->pending);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    if (g_str_equal (((PendingLaunch *) value)->desktop_id, desktop_id))
      return TRUE;

  return FALSE;
}

/* Ends one launch, and says so once its app has none pending. */
static void
end_launch (MockaWindowTracker *self,
            const gchar        *startup_id)
{
  PendingLaunch *launch = g_hash_table_lookup (self->pending, startup_id);
  g_autofree gchar *desktop_id = NULL;

  if (launch == NULL)
    return;

  desktop_id = g_strdup (launch->desktop_id);
  g_hash_table_remove (self->pending, startup_id);

  if (!mocka_window_tracker_is_launching (self, desktop_id))
    g_signal_emit (self, signals[SIGNAL_LAUNCH_STATE_CHANGED], 0,
                   desktop_id, FALSE);
}

/* Ends every launch of an app: a window of it appeared or became active. */
static void
end_app_launches (MockaWindowTracker *self,
                  const gchar        *desktop_id)
{
  GHashTableIter iter;
  gpointer value;
  gboolean ended = FALSE;

  g_hash_table_iter_init (&iter, self->pending);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      if (g_str_equal (((PendingLaunch *) value)->desktop_id, desktop_id))
        {
          g_hash_table_iter_remove (&iter);
          ended = TRUE;
        }
    }

  if (ended)
    g_signal_emit (self, signals[SIGNAL_LAUNCH_STATE_CHANGED], 0,
                   desktop_id, FALSE);
}

static gboolean
on_launch_timeout (gpointer user_data)
{
  PendingLaunch *launch = user_data;
  g_autofree gchar *startup_id = g_strdup (launch->startup_id);

  launch->timeout_id = 0;
  end_launch (launch->self, startup_id);
  return G_SOURCE_REMOVE;
}

static void
free_string (gpointer string)
{
  g_string_free (string, TRUE);
}

/* Ends the launches of a window's app, when it has a desktop entry. */
static void
end_launches_for_window (MockaWindowTracker *self,
                         WnckWindow         *window)
{
  MockaDockApp *app;

  if (window == NULL)
    return;

  app = mocka_dock_model_get_window_app (self->model, window);
  if (app != NULL && mocka_dock_app_get_entry (app) != NULL)
    end_app_launches (self, mocka_dock_app_get_key (app));
}

/*
 * Startup notification messages come to the root window in parts of 20
 * bytes, the first one of type _NET_STARTUP_INFO_BEGIN. A "remove" message
 * says an app finished starting.
 */
static GdkFilterReturn
on_root_event (GdkXEvent *gdk_xevent,
               GdkEvent  *event,
               gpointer   user_data)
{
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (user_data);
  XEvent *xevent = gdk_xevent;
  XClientMessageEvent *message = &xevent->xclient;
  GdkDisplay *display;
  gpointer sender;
  GString *text;
  const gchar *type;
  gsize length;

  if (xevent->type != ClientMessage || message->format != 8)
    return GDK_FILTER_CONTINUE;

  display = gdk_window_get_display (self->root);
  type = gdk_x11_get_xatom_name_for_display (display, message->message_type);
  sender = GUINT_TO_POINTER (message->window);

  if (g_str_equal (type, "_NET_STARTUP_INFO_BEGIN"))
    {
      text = g_string_new (NULL);
      g_hash_table_replace (self->messages, sender, text);
    }
  else if (g_str_equal (type, "_NET_STARTUP_INFO"))
    {
      text = g_hash_table_lookup (self->messages, sender);
      if (text == NULL)
        return GDK_FILTER_CONTINUE;
    }
  else
    {
      return GDK_FILTER_CONTINUE;
    }

  length = strnlen (message->data.b, sizeof message->data.b);
  g_string_append_len (text, message->data.b, length);

  if (length < sizeof message->data.b)
    {
      g_autofree gchar *startup_id =
        mocka_startup_message_get_value (text->str, "remove", "ID");

      g_hash_table_remove (self->messages, sender);
      if (startup_id != NULL)
        end_launch (self, startup_id);
    }

  return GDK_FILTER_CONTINUE;
}

/*
 * Startup notification messages are sent to the root window to clients
 * selecting property changes there. The dock shares its connection with
 * the panel, so it adds to the panel's selection instead of replacing it.
 */
static void
watch_startup_messages (MockaWindowTracker *self)
{
  GdkDisplay *display = gdk_display_get_default ();
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  XWindowAttributes attributes;
  Window xroot;

  self->root = g_object_ref (gdk_screen_get_root_window (gdk_screen_get_default ()));
  xroot = gdk_x11_window_get_xid (self->root);

  gdk_x11_display_error_trap_push (display);
  if (XGetWindowAttributes (dpy, xroot, &attributes))
    XSelectInput (dpy, xroot, attributes.your_event_mask | PropertyChangeMask);
  gdk_x11_display_error_trap_pop_ignored (display);

  gdk_window_add_filter (self->root, on_root_event, self);
}

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
 * where GTK apps set it (SPEC section 6, step 6).
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

/*
 * The executable of the window's process, from _NET_WM_PID (SPEC section 6,
 * step 5), or NULL when unknown.
 */
static gchar *
get_executable (WnckWindow *window)
{
#ifdef KERN_PROC_PATHNAME
  int pid = wnck_window_get_pid (window);
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, pid };
  char path[PATH_MAX];
  size_t length = sizeof path;

  if (pid > 0 && sysctl (mib, G_N_ELEMENTS (mib), path, &length, NULL, 0) == 0)
    return g_strdup (path);
#endif

  return NULL;
}

static void
update_window (MockaWindowTracker *self,
               WnckWindow         *window)
{
  g_autoptr(MockaAppEntry) entry = NULL;
  g_autofree gchar *key = NULL;
  g_autofree gchar *startup_id = NULL;
  g_autofree gchar *executable = NULL;
  const gchar *res_class;

  if (wnck_window_is_skip_tasklist (window))
    {
      mocka_dock_model_remove_window (self->model, window);
      return;
    }

  /* Only needed for windows of apps the dock launched. */
  if (g_hash_table_size (self->launches) > 0)
    startup_id = get_startup_id (window);

  executable = get_executable (window);

  res_class = wnck_window_get_class_group_name (window);
  entry = mocka_matcher_match (self->index,
                               wnck_window_get_class_instance_name (window),
                               res_class, executable, startup_id,
                               self->launches, NULL);
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
  MockaWindowTracker *self = MOCKA_WINDOW_TRACKER (user_data);

  track_window (self, window);

  /* A new window of an app that is starting (SPEC section 7). */
  end_launches_for_window (self, window);
}

static void
on_active_window_changed (WnckScreen *screen,
                          WnckWindow *previous,
                          gpointer    user_data)
{
  /* Covers single-instance apps that bring an existing window forward. */
  end_launches_for_window (MOCKA_WINDOW_TRACKER (user_data),
                           wnck_screen_get_active_window (screen));
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

  if (self->root != NULL)
    gdk_window_remove_filter (self->root, on_root_event, self);
  g_clear_object (&self->root);

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
  g_hash_table_unref (self->pending);
  g_hash_table_unref (self->messages);

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

  /* An app the dock launched starts or stops starting (SPEC section 7). */
  signals[SIGNAL_LAUNCH_STATE_CHANGED] =
    g_signal_new ("launch-state-changed", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
                  G_TYPE_STRING, G_TYPE_BOOLEAN);
}

static void
mocka_window_tracker_init (MockaWindowTracker *self)
{
  /* Kept for the session: apps keep their startup ID on later windows. */
  self->launches = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, g_free);
  self->pending = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                         (GDestroyNotify) pending_launch_free);
  self->messages = g_hash_table_new_full (NULL, NULL, NULL,
                                          free_string);
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
    {
      PendingLaunch *launch = g_new0 (PendingLaunch, 1);
      gboolean was_launching = mocka_window_tracker_is_launching (self, desktop_id);

      g_hash_table_insert (self->launches, g_strdup (startup_id),
                           g_strdup (desktop_id));

      launch->self = self;
      launch->startup_id = g_strdup (startup_id);
      launch->desktop_id = g_strdup (desktop_id);
      launch->timeout_id = g_timeout_add_seconds (LAUNCH_TIMEOUT_SECONDS,
                                                  on_launch_timeout, launch);
      g_hash_table_replace (self->pending, launch->startup_id, launch);

      if (!was_launching)
        g_signal_emit (self, signals[SIGNAL_LAUNCH_STATE_CHANGED], 0,
                       desktop_id, TRUE);
    }
}

/* Runs in the new process before the app starts; only async-signal-safe calls. */
static void
change_to_home (gpointer home)
{
  if (chdir (home) != 0)
    return;
}

/*
 * The app with the command of one of its desktop actions in place of its
 * own, so an action starts like the app itself. NULL when the action has no
 * command, which is allowed for apps started through D-Bus.
 */
static GDesktopAppInfo *
app_info_for_action (const gchar *path,
                     const gchar *action)
{
  g_autoptr(GKeyFile) keyfile = g_key_file_new ();
  g_autofree gchar *group = g_strconcat ("Desktop Action ", action, NULL);
  g_autofree gchar *exec = NULL;

  if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_KEEP_TRANSLATIONS,
                                  NULL))
    return NULL;

  exec = g_key_file_get_string (keyfile, group, G_KEY_FILE_DESKTOP_KEY_EXEC,
                                NULL);
  if (exec == NULL)
    return NULL;

  g_key_file_set_string (keyfile, G_KEY_FILE_DESKTOP_GROUP,
                         G_KEY_FILE_DESKTOP_KEY_EXEC, exec);
  g_key_file_remove_key (keyfile, G_KEY_FILE_DESKTOP_GROUP,
                         G_KEY_FILE_DESKTOP_KEY_DBUS_ACTIVATABLE, NULL);

  return g_desktop_app_info_new_from_keyfile (keyfile);
}

/*
 * Launches an app, with startup notification and the time of the click so
 * its window gets focus. With action set, runs that desktop action instead,
 * and with uris set, opens those files (SPEC section 9.1). The startup ID is
 * remembered, so the app's windows are matched to it even when their class
 * matches no desktop entry (SPEC section 6, step 6).
 */
gboolean
mocka_window_tracker_launch (MockaWindowTracker  *self,
                             MockaAppEntry       *entry,
                             const gchar         *action,
                             const gchar * const *uris,
                             GdkDisplay          *display,
                             guint32              timestamp,
                             GError             **error)
{
  g_autoptr(GDesktopAppInfo) info = NULL;
  g_autoptr(GdkAppLaunchContext) context = NULL;
  g_autoptr(GList) uri_list = NULL;
  g_autofree gchar *path = NULL;
  guint i;

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

  if (action != NULL)
    {
      GDesktopAppInfo *action_info = app_info_for_action (entry->path, action);

      if (action_info == NULL)
        {
          g_desktop_app_info_launch_action (info, action,
                                            G_APP_LAUNCH_CONTEXT (context));
          return TRUE;
        }
      g_object_unref (info);
      info = action_info;
    }

  for (i = 0; uris != NULL && uris[i] != NULL; i++)
    uri_list = g_list_append (uri_list, (gpointer) uris[i]);

  g_object_set_data_full (G_OBJECT (context), "mocka-desktop-id",
                          g_strdup (entry->id), g_free);
  g_signal_connect (context, "launched", G_CALLBACK (on_launched), self);

  /*
   * The dock runs inside mate-panel, whose working directory is wherever the
   * panel was started. Like a menu, start apps in the home directory
   * instead, unless their entry sets Path.
   */
  path = g_desktop_app_info_get_string (info, G_KEY_FILE_DESKTOP_KEY_PATH);

  return g_desktop_app_info_launch_uris_as_manager (info, uri_list,
      G_APP_LAUNCH_CONTEXT (context), G_SPAWN_SEARCH_PATH,
      path == NULL ? change_to_home : NULL, (gpointer) g_get_home_dir (),
      NULL, NULL, error);
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
  g_signal_connect (self->screen, "active-window-changed",
                    G_CALLBACK (on_active_window_changed), self);
  watch_startup_messages (self);
  g_signal_connect (self->screen, "active-workspace-changed",
                    G_CALLBACK (on_active_workspace_changed), self);
  g_signal_connect (self->index, "changed",
                    G_CALLBACK (on_index_changed), self);

  /* Existing windows, in the order they were mapped. */
  for (l = wnck_screen_get_windows (self->screen); l != NULL; l = l->next)
    track_window (self, l->data);

  return self;
}