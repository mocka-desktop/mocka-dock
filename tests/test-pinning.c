/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Pinned list edits and dropped desktop entry files (SPEC section 10).
 */

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "desktop-import.h"
#include "pinned-list.h"

/* The list as space-separated IDs. */
static void
assert_list (gchar       **ids,
             const gchar  *expected)
{
  g_autofree gchar *joined = g_strjoinv (" ", ids);

  g_assert_cmpstr (joined, ==, expected);
  g_strfreev (ids);
}

static void
test_insert (void)
{
  const gchar *ids[] = { "a", "b", "c", NULL };

  assert_list (mocka_pinned_list_insert (ids, "x", 0), "x a b c");
  assert_list (mocka_pinned_list_insert (ids, "x", 1), "a x b c");
  assert_list (mocka_pinned_list_insert (ids, "x", 3), "a b c x");
  assert_list (mocka_pinned_list_insert (ids, "x", 99), "a b c x");
}

/* Dropping a pinned app at a gap moves it there. */
static void
test_move (void)
{
  const gchar *ids[] = { "a", "b", "c", NULL };

  assert_list (mocka_pinned_list_insert (ids, "a", 2), "b a c");
  assert_list (mocka_pinned_list_insert (ids, "a", 3), "b c a");
  assert_list (mocka_pinned_list_insert (ids, "c", 0), "c a b");

  /* The gaps on either side of an app leave it in place. */
  assert_list (mocka_pinned_list_insert (ids, "b", 1), "a b c");
  assert_list (mocka_pinned_list_insert (ids, "b", 2), "a b c");
}

/* Unpinning reports where the app was, and Undo puts it back there. */
static void
test_remove_and_undo (void)
{
  const gchar *ids[] = { "a", "b", "c", NULL };
  g_auto(GStrv) removed = NULL;
  guint position;

  removed = mocka_pinned_list_remove (ids, "b", &position);
  g_assert_cmpuint (position, ==, 1);
  assert_list (mocka_pinned_list_insert ((const gchar * const *) removed, "b",
                                         position), "a b c");

  assert_list (mocka_pinned_list_remove (ids, "x", &position), "a b c");
  g_assert_cmpuint (position, ==, 3);
}

static void
test_empty (void)
{
  const gchar *ids[] = { NULL };

  assert_list (mocka_pinned_list_insert (ids, "a", 0), "a");
  assert_list (mocka_pinned_list_remove (ids, "a", NULL), "");
}

/* Directories for the import tests: two applications dirs and elsewhere. */
typedef struct
{
  gchar *root;
  gchar *user_dir;
  gchar *system_dir;
  gchar *elsewhere;
} Dirs;

static void
dirs_setup (Dirs          *dirs,
            gconstpointer  data)
{
  dirs->root = g_dir_make_tmp ("mocka-dock-test-XXXXXX", NULL);
  dirs->user_dir = g_build_filename (dirs->root, "user", "applications", NULL);
  dirs->system_dir = g_build_filename (dirs->root, "system", "applications", NULL);
  dirs->elsewhere = g_build_filename (dirs->root, "Desktop", NULL);

  g_assert_cmpint (g_mkdir_with_parents (dirs->system_dir, 0700), ==, 0);
  g_assert_cmpint (g_mkdir_with_parents (dirs->elsewhere, 0700), ==, 0);
}

static void
remove_tree (const gchar *path)
{
  g_autoptr(GDir) dir = g_dir_open (path, 0, NULL);
  const gchar *name;

  while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree gchar *child = g_build_filename (path, name, NULL);

      if (g_file_test (child, G_FILE_TEST_IS_DIR))
        remove_tree (child);
      else
        g_remove (child);
    }

  g_rmdir (path);
}

static void
dirs_teardown (Dirs          *dirs,
               gconstpointer  data)
{
  remove_tree (dirs->root);
  g_free (dirs->root);
  g_free (dirs->user_dir);
  g_free (dirs->system_dir);
  g_free (dirs->elsewhere);
}

static gchar *
write_file (const gchar *dir,
            const gchar *name,
            const gchar *contents)
{
  gchar *path = g_build_filename (dir, name, NULL);
  g_autofree gchar *parent = g_path_get_dirname (path);

  g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
  g_assert_true (g_file_set_contents (path, contents, -1, NULL));
  return path;
}

#define APP_ENTRY "[Desktop Entry]\nType=Application\nName=Tool\nExec=tool\n"

static gchar *
import (Dirs        *dirs,
        const gchar *path,
        GError     **error)
{
  const gchar *app_dirs[] = { dirs->user_dir, dirs->system_dir, NULL };

  return mocka_desktop_import (path, app_dirs, dirs->user_dir, error);
}

/* A file in an applications directory keeps its ID, subdirectories included. */
static void
test_import_installed (Dirs          *dirs,
                       gconstpointer  data)
{
  g_autofree gchar *path = write_file (dirs->system_dir, "wine/Programs/tool.desktop",
                                       APP_ENTRY);
  g_autofree gchar *id = import (dirs, path, NULL);

  g_assert_cmpstr (id, ==, "wine-Programs-tool.desktop");
  g_assert_false (g_file_test (dirs->user_dir, G_FILE_TEST_EXISTS));
}

/* Any other file is copied into the user's applications directory. */
static void
test_import_copy (Dirs          *dirs,
                  gconstpointer  data)
{
  g_autofree gchar *path = write_file (dirs->elsewhere, "tool.desktop", APP_ENTRY);
  g_autofree gchar *id = import (dirs, path, NULL);
  g_autofree gchar *copy = g_build_filename (dirs->user_dir, "tool.desktop", NULL);
  g_autofree gchar *contents = NULL;

  g_assert_cmpstr (id, ==, "tool.desktop");
  g_assert_true (g_file_get_contents (copy, &contents, NULL, NULL));
  g_assert_cmpstr (contents, ==, APP_ENTRY);
}

/* Dropping the same file again reuses the copy; a different one gets a new name. */
static void
test_import_names (Dirs          *dirs,
                   gconstpointer  data)
{
  g_autofree gchar *path = write_file (dirs->elsewhere, "tool.desktop", APP_ENTRY);
  g_autofree gchar *other_dir = g_build_filename (dirs->elsewhere, "other", NULL);
  g_autofree gchar *other = write_file (other_dir, "tool.desktop",
                                        "[Desktop Entry]\nType=Application\n"
                                        "Name=Other\nExec=other\n");
  g_autofree gchar *first = import (dirs, path, NULL);
  g_autofree gchar *again = import (dirs, path, NULL);
  g_autofree gchar *second = import (dirs, other, NULL);

  g_assert_cmpstr (first, ==, "tool.desktop");
  g_assert_cmpstr (again, ==, "tool.desktop");
  g_assert_cmpstr (second, ==, "tool-2.desktop");
}

/* Only application desktop entries are accepted. */
static void
test_import_refused (Dirs          *dirs,
                     gconstpointer  data)
{
  g_autofree gchar *link = write_file (dirs->elsewhere, "site.desktop",
                                       "[Desktop Entry]\nType=Link\nName=Site\n"
                                       "URL=https://example.org\n");
  g_autofree gchar *text = write_file (dirs->elsewhere, "notes.txt", "hello\n");
  g_autofree gchar *broken = write_file (dirs->elsewhere, "broken.desktop",
                                         "not a key file\n");
  g_autoptr(GError) error = NULL;
  gchar *id;

  id = import (dirs, link, &error);
  g_assert_null (id);
  g_assert_nonnull (error);
  g_clear_error (&error);

  id = import (dirs, text, &error);
  g_assert_null (id);
  g_assert_nonnull (error);
  g_clear_error (&error);

  id = import (dirs, broken, &error);
  g_assert_null (id);
  g_assert_nonnull (error);

  g_assert_false (g_file_test (dirs->user_dir, G_FILE_TEST_EXISTS));
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pinned-list/insert", test_insert);
  g_test_add_func ("/pinned-list/move", test_move);
  g_test_add_func ("/pinned-list/remove-and-undo", test_remove_and_undo);
  g_test_add_func ("/pinned-list/empty", test_empty);
  g_test_add ("/desktop-import/installed", Dirs, NULL,
              dirs_setup, test_import_installed, dirs_teardown);
  g_test_add ("/desktop-import/copy", Dirs, NULL,
              dirs_setup, test_import_copy, dirs_teardown);
  g_test_add ("/desktop-import/names", Dirs, NULL,
              dirs_setup, test_import_names, dirs_teardown);
  g_test_add ("/desktop-import/refused", Dirs, NULL,
              dirs_setup, test_import_refused, dirs_teardown);

  return g_test_run ();
}