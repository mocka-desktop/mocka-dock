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

  mocka_dock_model_add_window (fixture->model, WINDOW (1), "a.desktop", NULL);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "b.desktop", NULL);

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
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "b.desktop", NULL);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "b.desktop", NULL);
  mocka_dock_model_add_window (fixture->model, WINDOW (4), "c.desktop", NULL);

  assert_order (fixture->model, "b.desktop a.desktop c.desktop");
}

/* When the last window of an app closes, its button is removed. */
static void
test_last_window_closes (Fixture       *fixture,
                         gconstpointer  data)
{
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "a.desktop", NULL);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL);
  mocka_dock_model_add_window (fixture->model, WINDOW (3), "b.desktop", NULL);

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
  mocka_dock_model_add_window (fixture->model, WINDOW (1), "class:Soffice", NULL);
  mocka_dock_model_add_window (fixture->model, WINDOW (2), "a.desktop", NULL);

  mocka_dock_model_add_window (fixture->model, WINDOW (1),
                               "libreoffice-writer.desktop", NULL);
  assert_order (fixture->model, "a.desktop libreoffice-writer.desktop");
  g_assert_null (mocka_dock_model_lookup (fixture->model, "class:Soffice"));

  /* Moving into an app that already has a button joins it. */
  mocka_dock_model_add_window (fixture->model, WINDOW (2),
                               "libreoffice-writer.desktop", NULL);
  assert_order (fixture->model, "libreoffice-writer.desktop");
  g_assert_cmpuint (mocka_dock_app_get_windows (
      mocka_dock_model_lookup (fixture->model, "libreoffice-writer.desktop"))->len,
      ==, 2);

  /* Matching again to the same app changes nothing. */
  fixture->changes = 0;
  mocka_dock_model_add_window (fixture->model, WINDOW (2),
                               "libreoffice-writer.desktop", NULL);
  g_assert_cmpuint (fixture->changes, ==, 0);
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
  g_test_add_func ("/dock-model/key-for", test_key_for);

  return g_test_run ();
}