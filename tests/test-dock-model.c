/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Dock model tests (SPEC section 5). Windows are plain integers cast to
 * pointers; the model never looks inside them.
 */

#include <glib.h>

#include "dock-model.h"

#define WINDOW(n) GUINT_TO_POINTER (n)

typedef struct
{
  MockaDockModel *model;
  guint changes;
  guint last_position;
  guint last_removed;
  guint last_added;
} Fixture;

static void
on_items_changed (GListModel *list,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
  Fixture *fixture = user_data;

  fixture->changes++;
  fixture->last_position = position;
  fixture->last_removed = removed;
  fixture->last_added = added;
}

static void
fixture_setup (Fixture       *fixture,
               gconstpointer  data)
{
  fixture->model = mocka_dock_model_new ();
  g_signal_connect (fixture->model, "items-changed",
                    G_CALLBACK (on_items_changed), fixture);
}

static void
fixture_teardown (Fixture       *fixture,
                  gconstpointer  data)
{
  g_object_unref (fixture->model);
}

/* The keys of the apps in dock order, joined by spaces. */
static gchar *
dock_order (MockaDockModel *model)
{
  GString *order = g_string_new (NULL);
  guint i, n = g_list_model_get_n_items (G_LIST_MODEL (model));

  for (i = 0; i < n; i++)
    {
      g_autoptr(MockaDockApp) app = g_list_model_get_item (G_LIST_MODEL (model), i);

      if (i > 0)
        g_string_append_c (order, ' ');
      g_string_append (order, mocka_dock_app_get_key (app));
    }

  return g_string_free (order, FALSE);
}

static void
assert_order (MockaDockModel *model,
              const gchar    *expected)
{
  g_autofree gchar *order = dock_order (model);

  g_assert_cmpstr (order, ==, expected);
}

/* All windows of the same app share one button. */
static void
test_grouping (Fixture       *fixture,
               gconstpointer  data)
{
  MockaDockApp *app;

  mocka_dock_model_add_window (fixture->model, WINDOW (1), "a.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "b.desktop", NULL, TRUE);

  assert_order (fixture->model, "a.desktop b.desktop");
  app = mocka_dock_model_lookup (fixture->model, "a.desktop");
  g_assert_cmpuint (mocka_dock_app_get_windows (app)->len, ==, 2);
  g_assert_true (mocka_dock_model_get_window_app (fixture->model, WINDOW (2)) == app);
  g_assert_cmpuint (fixture->changes, ==, 2);
}

/* Apps appear in the order their first window appeared. */
static void
test_start_order (Fixture       *fixture,
                  gconstpointer  data)
{
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "b.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "b.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (4), "c.desktop", NULL, TRUE);

  assert_order (fixture->model, "b.desktop a.desktop c.desktop");
}

/* When the last window of an app closes, its button is removed. */
static void
test_last_window_closes (Fixture       *fixture,
                         gconstpointer  data)
{
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "a.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "b.desktop", NULL, TRUE);

  mocka_dock_model_remove_window (fixture->model, WINDOW (1));
  assert_order (fixture->model, "a.desktop b.desktop");

  mocka_dock_model_remove_window (fixture->model, WINDOW (2));
  assert_order (fixture->model, "b.desktop");
  g_assert_null (mocka_dock_model_lookup (fixture->model, "a.desktop"));
  g_assert_cmpuint (fixture->last_position, ==, 0);
  g_assert_cmpuint (fixture->last_removed, ==, 1);
  g_assert_cmpuint (fixture->last_added, ==, 0);

  /* Removing an unknown window does nothing. */
  fixture->changes = 0;
  mocka_dock_model_remove_window (fixture->model, WINDOW (9));
  g_assert_cmpuint (fixture->changes, ==, 0);
}

/*
 * A window whose class changes moves to its new app (SPEC section 6),
 * which is created at the end when it has no button yet. Its old app goes
 * away when that was its last window.
 */
static void
test_class_change (Fixture       *fixture,
                   gconstpointer  data)
{
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "class:Soffice", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL, TRUE);

  mocka_dock_model_add_window (fixture->model, WINDOW (1),
                               "libreoffice-writer.desktop", NULL, TRUE);
  assert_order (fixture->model, "a.desktop libreoffice-writer.desktop");
  g_assert_null (mocka_dock_model_lookup (fixture->model, "class:Soffice"));

  /* Moving into an app that already has a button joins it. */
  mocka_dock_model_add_window (fixture->model, WINDOW (2),
                               "libreoffice-writer.desktop", NULL, TRUE);
  assert_order (fixture->model, "libreoffice-writer.desktop");
  g_assert_cmpuint (mocka_dock_app_get_windows (
      mocka_dock_model_lookup (fixture->model, "libreoffice-writer.desktop"))->len,
      ==, 2);

  /* Matching again to the same app changes nothing. */
  fixture->changes = 0;
  mocka_dock_model_add_window (fixture->model, WINDOW (2),
                               "libreoffice-writer.desktop", NULL, TRUE);
  g_assert_cmpuint (fixture->changes, ==, 0);
}

static void
on_windows_changed (MockaDockApp *app,
                    gpointer      user_data)
{
  (*(guint *) user_data)++;
}

/*
 * Current workspace mode (SPEC section 5): an app whose windows are all on
 * other workspaces has no button, and gets it back in its start-order place.
 */
static void
test_hidden_app (Fixture       *fixture,
                 gconstpointer  data)
{
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "a.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "b.desktop", NULL, FALSE);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "c.desktop", NULL, TRUE);
  assert_order (fixture->model, "a.desktop c.desktop");

  /* Hidden apps are still known, and come back between a and c. */
  g_assert_nonnull (mocka_dock_model_lookup (fixture->model, "b.desktop"));
  mocka_dock_model_set_window_visible (fixture->model, WINDOW (2), TRUE);
  assert_order (fixture->model, "a.desktop b.desktop c.desktop");
  g_assert_cmpuint (fixture->last_position, ==, 1);
  g_assert_cmpuint (fixture->last_added, ==, 1);

  /* Switching workspace hides a and b. */
  mocka_dock_model_set_window_visible (fixture->model, WINDOW (1), FALSE);
  mocka_dock_model_set_window_visible (fixture->model, WINDOW (2), FALSE);
  assert_order (fixture->model, "c.desktop");

  /* A new app still goes after all apps seen before, hidden or not. */
  mocka_dock_model_add_window (fixture->model, WINDOW (4), "d.desktop", NULL, TRUE);
  mocka_dock_model_set_window_visible (fixture->model, WINDOW (1), TRUE);
  assert_order (fixture->model, "a.desktop c.desktop d.desktop");
}

/* Counts and clicks use only the shown windows. */
static void
test_shown_windows (Fixture       *fixture,
                    gconstpointer  data)
{
  MockaDockApp *app;
  guint changed = 0;
  GPtrArray *windows;

  mocka_dock_model_add_window (fixture->model, WINDOW (1), "a.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL, FALSE);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "a.desktop", NULL, TRUE);
  app = mocka_dock_model_lookup (fixture->model, "a.desktop");
  g_signal_connect (app, "windows-changed", G_CALLBACK (on_windows_changed), &changed);

  windows = mocka_dock_app_get_windows (app);
  g_assert_cmpuint (windows->len, ==, 2);
  g_assert_true (g_ptr_array_index (windows, 0) == WINDOW (1));
  g_assert_true (g_ptr_array_index (windows, 1) == WINDOW (3));

  /* Shown again: back in its original place among the windows. */
  mocka_dock_model_set_window_visible (fixture->model, WINDOW (2), TRUE);
  g_assert_cmpuint (windows->len, ==, 3);
  g_assert_true (g_ptr_array_index (windows, 1) == WINDOW (2));
  g_assert_cmpuint (changed, ==, 1);

  /* No change, no signal. */
  mocka_dock_model_set_window_visible (fixture->model, WINDOW (2), TRUE);
  g_assert_cmpuint (changed, ==, 1);
}

/* A hidden app is forgotten when its last window closes. */
static void
test_hidden_closes (Fixture       *fixture,
                    gconstpointer  data)
{
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "a.desktop", NULL, FALSE);
  g_assert_cmpuint (fixture->changes, ==, 0);

  mocka_dock_model_remove_window (fixture->model, WINDOW (1));
  g_assert_null (mocka_dock_model_lookup (fixture->model, "a.desktop"));
  g_assert_cmpuint (fixture->changes, ==, 0);

  /* Showing a window that is not in the model does nothing. */
  mocka_dock_model_set_window_visible (fixture->model, WINDOW (1), TRUE);
  assert_order (fixture->model, "");
}

/* A window moving to another app keeps its hidden state. */
static void
test_hidden_class_change (Fixture       *fixture,
                          gconstpointer  data)
{
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "class:Soffice", NULL, FALSE);
  mocka_dock_model_add_window (fixture->model, WINDOW (1),
                               "libreoffice-writer.desktop", NULL, FALSE);
  assert_order (fixture->model, "");
  g_assert_null (mocka_dock_model_lookup (fixture->model, "class:Soffice"));

  mocka_dock_model_set_window_visible (fixture->model, WINDOW (1), TRUE);
  assert_order (fixture->model, "libreoffice-writer.desktop");
}

/* Pins the apps with these space-separated IDs, in order. */
static void
set_pinned (MockaDockModel *model,
            const gchar    *ids)
{
  g_auto(GStrv) split = g_strsplit (ids, " ", -1);
  g_autoptr(GPtrArray) entries =
    g_ptr_array_new_with_free_func ((GDestroyNotify) mocka_app_entry_unref);
  guint i;

  for (i = 0; split[i] != NULL; i++)
    {
      MockaAppEntry *entry;

      if (*split[i] == '\0')
        continue;
      entry = g_rc_box_new0 (MockaAppEntry);
      entry->id = g_strdup (split[i]);
      g_ptr_array_add (entries, entry);
    }

  mocka_dock_model_set_pinned (model, entries);
}

/* Pinned apps first, in the user's order, running or not (SPEC section 5). */
static void
test_pinned_first (Fixture       *fixture,
                   gconstpointer  data)
{
  MockaDockApp *app;

  mocka_dock_model_add_window (fixture->model, WINDOW (1), "b.desktop", NULL, TRUE);
  set_pinned (fixture->model, "c.desktop a.desktop");
  assert_order (fixture->model, "c.desktop a.desktop b.desktop");

  app = mocka_dock_model_lookup (fixture->model, "c.desktop");
  g_assert_true (mocka_dock_app_get_pinned (app));
  g_assert_cmpuint (mocka_dock_app_get_windows (app)->len, ==, 0);
  g_assert_nonnull (mocka_dock_app_get_entry (app));

  /* A pinned app starting keeps its button. */
  fixture->changes = 0;
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL, TRUE);
  assert_order (fixture->model, "c.desktop a.desktop b.desktop");
  g_assert_cmpuint (fixture->changes, ==, 0);

  /* Its last window closing does not remove it either. */
  mocka_dock_model_remove_window (fixture->model, WINDOW (2));
  assert_order (fixture->model, "c.desktop a.desktop b.desktop");
  g_assert_cmpuint (fixture->changes, ==, 0);
}

/*
 * Unpinning: a running app moves after the pinned apps, at its place in
 * start order; an app that is not running goes away.
 */
static void
test_unpin (Fixture       *fixture,
            gconstpointer  data)
{
  set_pinned (fixture->model, "a.desktop b.desktop");
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "x.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL, TRUE);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "y.desktop", NULL, TRUE);
  assert_order (fixture->model, "a.desktop b.desktop x.desktop y.desktop");

  set_pinned (fixture->model, "");
  assert_order (fixture->model, "x.desktop a.desktop y.desktop");
  g_assert_null (mocka_dock_model_lookup (fixture->model, "b.desktop"));
}

/* Reordering pinned apps, and a duplicate ID counting once. */
static void
test_pinned_order (Fixture       *fixture,
                   gconstpointer  data)
{
  set_pinned (fixture->model, "a.desktop b.desktop c.desktop");
  set_pinned (fixture->model, "c.desktop a.desktop c.desktop b.desktop");
  assert_order (fixture->model, "c.desktop a.desktop b.desktop");
}

/* Only the part of the dock that changed is reported. */
static void
test_minimal_change (Fixture       *fixture,
                     gconstpointer  data)
{
  set_pinned (fixture->model, "a.desktop b.desktop");
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "x.desktop", NULL, TRUE);
  g_assert_cmpuint (fixture->last_position, ==, 2);
  g_assert_cmpuint (fixture->last_removed, ==, 0);
  g_assert_cmpuint (fixture->last_added, ==, 1);

  set_pinned (fixture->model, "a.desktop z.desktop b.desktop");
  g_assert_cmpuint (fixture->last_position, ==, 1);
  g_assert_cmpuint (fixture->last_removed, ==, 0);
  g_assert_cmpuint (fixture->last_added, ==, 1);
}

/* A pinned app with its windows on other workspaces shows as not running. */
static void
test_pinned_hidden (Fixture       *fixture,
                    gconstpointer  data)
{
  set_pinned (fixture->model, "a.desktop");
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "a.desktop", NULL, FALSE);
  assert_order (fixture->model, "a.desktop");
  g_assert_cmpuint (mocka_dock_app_get_windows (
      mocka_dock_model_lookup (fixture->model, "a.desktop"))->len, ==, 0);
}

static void
test_key_for (void)
{
  g_autofree gchar *fallback = mocka_dock_app_key_for (NULL, "Soffice");

  g_assert_cmpstr (fallback, ==, "class:Soffice");
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/dock-model/grouping", Fixture, NULL,
              fixture_setup, test_grouping, fixture_teardown);
  g_test_add ("/dock-model/start-order", Fixture, NULL,
              fixture_setup, test_start_order, fixture_teardown);
  g_test_add ("/dock-model/last-window-closes", Fixture, NULL,
              fixture_setup, test_last_window_closes, fixture_teardown);
  g_test_add ("/dock-model/class-change", Fixture, NULL,
              fixture_setup, test_class_change, fixture_teardown);
  g_test_add ("/dock-model/hidden-app", Fixture, NULL,
              fixture_setup, test_hidden_app, fixture_teardown);
  g_test_add ("/dock-model/shown-windows", Fixture, NULL,
              fixture_setup, test_shown_windows, fixture_teardown);
  g_test_add ("/dock-model/hidden-closes", Fixture, NULL,
              fixture_setup, test_hidden_closes, fixture_teardown);
  g_test_add ("/dock-model/hidden-class-change", Fixture, NULL,
              fixture_setup, test_hidden_class_change, fixture_teardown);
  g_test_add ("/dock-model/pinned-first", Fixture, NULL,
              fixture_setup, test_pinned_first, fixture_teardown);
  g_test_add ("/dock-model/unpin", Fixture, NULL,
              fixture_setup, test_unpin, fixture_teardown);
  g_test_add ("/dock-model/pinned-order", Fixture, NULL,
              fixture_setup, test_pinned_order, fixture_teardown);
  g_test_add ("/dock-model/minimal-change", Fixture, NULL,
              fixture_setup, test_minimal_change, fixture_teardown);
  g_test_add ("/dock-model/pinned-hidden", Fixture, NULL,
              fixture_setup, test_pinned_hidden, fixture_teardown);
  g_test_add_func ("/dock-model/key-for", test_key_for);

  return g_test_run ();
}