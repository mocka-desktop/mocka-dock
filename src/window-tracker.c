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
 */

#include "config.h"

#include "window-tracker.h"

#include "matcher.h"

struct _MockaWindowTracker
{
  GObject parent_instance;

  WnckScreen *screen;
  MockaAppIndex *index;
  MockaDockModel *model;
};

G_DEFINE_TYPE (MockaWindowTracker, mocka_window_tracker, G_TYPE_OBJECT)

static void
update_window (MockaWindowTracker *self,
               WnckWindow         *window)
{
  g_autoptr(MockaAppEntry) entry = NULL;
  g_autofree gchar *key = NULL;
  const gchar *res_class;

  if (wnck_window_is_skip_tasklist (window))
    {
      mocka_dock_model_remove_window (self->model, window);
      return;
    }

  res_class = wnck_window_get_class_group_name (window);
  entry = mocka_matcher_match (self->index,
                               wnck_window_get_class_instance_name (window),
                               res_class, NULL, NULL, NULL);
  key = mocka_dock_app_key_for (entry, res_class);

  mocka_dock_model_add_window (self->model, window, key, entry);
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
mocka_window_tracker_class_init (MockaWindowTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = mocka_window_tracker_dispose;
}

static void
mocka_window_tracker_init (MockaWindowTracker *self)
{
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
  g_signal_connect (self->index, "changed",
                    G_CALLBACK (on_index_changed), self);

  /* Existing windows, in the order they were mapped. */
  for (l = wnck_screen_get_windows (self->screen); l != NULL; l = l->next)
    track_window (self, l->data);

  return self;
}