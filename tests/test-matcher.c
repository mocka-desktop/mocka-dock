/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Matcher tests (SPEC section 6). Every sample in docs/test-data/ is checked
 * against the desktop entries in docs/test-data/applications/, then made-up
 * cases cover the rules no recorded sample exercises.
 */

#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "app-index.h"
#include "matcher.h"

static MockaAppIndex *fixture_index;

typedef struct
{
  gchar *expect;
  gchar *instance;
  gchar *res_class;
  gchar *startup_id;
  gchar *executable;
} Sample;

static void
sample_free (Sample *sample)
{
  g_free (sample->expect);
  g_free (sample->instance);
  g_free (sample->res_class);
  g_free (sample->startup_id);
  g_free (sample->executable);
  g_free (sample);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Sample, sample_free)

/* Returns the n-th quoted string of an xprop line, or NULL. */
static gchar *
quoted (const gchar *line,
        guint        n)
{
  g_autoptr(GRegex) regex = g_regex_new ("\"([^\"]*)\"", 0, 0, NULL);
  g_autoptr(GMatchInfo) info = NULL;
  guint i;

  g_regex_match (regex, line, 0, &info);
  for (i = 0; i < n && g_match_info_matches (info); i++)
    g_match_info_next (info, NULL);

  return g_match_info_matches (info) ? g_match_info_fetch (info, 1) : NULL;
}

/*
 * Reads a sample (docs/test-data/README.md). The last WM_CLASS value wins,
 * and the client leader's startup ID is used when the window has none.
 */
static Sample *
sample_load (const gchar *path)
{
  g_autofree gchar *contents = NULL;
  g_auto(GStrv) lines = NULL;
  Sample *sample = g_new0 (Sample, 1);
  guint i;

  g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
  lines = g_strsplit (contents, "\n", -1);

  for (i = 0; lines[i] != NULL; i++)
    {
      const gchar *line = lines[i];

      if (g_str_has_prefix (line, "# expect: "))
        {
          sample->expect = g_strdup (line + strlen ("# expect: "));
        }
      else if (g_str_has_prefix (line, "WM_CLASS = ")
               || g_str_has_prefix (line, "# WM_CLASS = "))
        {
          g_free (sample->instance);
          g_free (sample->res_class);
          sample->instance = quoted (line, 0);
          sample->res_class = quoted (line, 1);
        }
      else if (g_str_has_prefix (line, "_NET_STARTUP_ID = "))
        {
          g_free (sample->startup_id);
          sample->startup_id = quoted (line, 0);
        }
      else if (g_str_has_prefix (line, "executable = "))
        {
          g_free (sample->executable);
          sample->executable = g_strdup (line + strlen ("executable = "));
        }
      else if (g_str_has_prefix (line, "leader _NET_STARTUP_ID = ")
               && sample->startup_id == NULL)
        {
          sample->startup_id = quoted (line, 0);
        }
    }

  return sample;
}

static void
test_sample (gconstpointer data)
{
  g_autofree gchar *path = g_build_filename (TEST_DATA_DIR, data, NULL);
  g_autoptr(Sample) sample = sample_load (path);
  g_autoptr(MockaAppEntry) entry = NULL;
  MockaMatchStep step;

  g_assert_nonnull (sample->expect);
  g_assert_nonnull (sample->instance);
  g_assert_nonnull (sample->res_class);

  entry = mocka_matcher_match (fixture_index, sample->instance,
                               sample->res_class, sample->executable,
                               sample->startup_id, NULL, &step);

  if (strcmp (sample->expect, "none") == 0)
    {
      g_assert_null (entry);
      g_assert_cmpint (step, ==, MOCKA_MATCH_NONE);
    }
  else
    {
      g_assert_nonnull (entry);
      g_assert_cmpstr (entry->id, ==, sample->expect);
    }
}

/* Helpers for made-up desktop entry directories. */

static void
write_entry (const gchar *dir,
             const gchar *relative_path,
             const gchar *keys)
{
  g_autofree gchar *path = g_build_filename (dir, relative_path, NULL);
  g_autofree gchar *parent = g_path_get_dirname (path);
  g_autofree gchar *contents = g_strconcat ("[Desktop Entry]\n"
                                            "Type=Application\n"
                                            "Name=Test\n", keys, NULL);

  g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
  g_assert_true (g_file_set_contents (path, contents, -1, NULL));
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

static gchar *
make_dir (void)
{
  gchar *dir = g_dir_make_tmp ("mocka-dock-test-XXXXXX", NULL);

  g_assert_nonnull (dir);
  return dir;
}

static MockaAppIndex *
index_for (const gchar *dir)
{
  const gchar *dirs[] = { dir, NULL };

  return mocka_app_index_new (dirs);
}

static void
assert_match (MockaAppIndex  *index,
              const gchar    *instance,
              const gchar    *res_class,
              const gchar    *expect,
              MockaMatchStep  expect_step)
{
  g_autoptr(MockaAppEntry) entry = NULL;
  MockaMatchStep step;

  entry = mocka_matcher_match (index, instance, res_class, NULL, NULL, NULL,
                               &step);
  g_assert_nonnull (entry);
  g_assert_cmpstr (entry->id, ==, expect);
  g_assert_cmpint (step, ==, expect_step);
}

/* A hidden entry never wins over a visible one, even at an earlier step. */
static void
test_hidden_loses (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "files.desktop", "Exec=/usr/bin/tool --browser %U\n");
  write_entry (dir, "tool.desktop", "Exec=tool\nNoDisplay=true\n");
  index = index_for (dir);
  assert_match (index, "tool", "Tool", "files.desktop", MOCKA_MATCH_PROGRAM);

  remove_tree (dir);
}

/* A hidden entry is used when no visible entry matches. */
static void
test_hidden_fallback (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "tool.desktop", "Exec=tool\nNoDisplay=true\n");
  write_entry (dir, "other.desktop", "Exec=other\n");
  index = index_for (dir);
  assert_match (index, "tool", "Tool", "tool.desktop", MOCKA_MATCH_ID);

  remove_tree (dir);
}

/* No extra Exec arguments first, then by ID. */
static void
test_tie_break (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "a.desktop", "Exec=tool --window\n");
  write_entry (dir, "c.desktop", "Exec=tool\n");
  write_entry (dir, "b.desktop", "Exec=tool %U\n");
  index = index_for (dir);
  assert_match (index, "tool", "Tool", "b.desktop", MOCKA_MATCH_PROGRAM);

  remove_tree (dir);
}

/* TryExec names the program when present. */
static void
test_try_exec (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "launcher.desktop",
               "TryExec=/usr/local/bin/real-tool\nExec=wrapper %U\n");
  index = index_for (dir);
  assert_match (index, "real-tool", "Real-tool", "launcher.desktop",
                MOCKA_MATCH_PROGRAM);

  remove_tree (dir);
}

/* The instance name is tried before the class name within a step. */
static void
test_instance_first (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "browser.desktop", "Exec=browser\nStartupWMClass=Browser\n");
  write_entry (dir, "webapp.desktop", "Exec=browser --app\nStartupWMClass=crx_x\n");
  index = index_for (dir);
  assert_match (index, "crx_x", "Browser", "webapp.desktop",
                MOCKA_MATCH_STARTUP_WM_CLASS_INSTANCE);

  remove_tree (dir);
}

/* Step 6: a window the dock launched, matched by its startup ID. */
static void
test_startup_id (void)
{
  g_autoptr(GHashTable) launches = g_hash_table_new (g_str_hash, g_str_equal);
  g_autoptr(MockaAppEntry) entry = NULL;
  MockaMatchStep step;

  g_hash_table_insert (launches, "mocka-dock-1_TIME0", "libreoffice-writer.desktop");

  entry = mocka_matcher_match (fixture_index, "soffice", "Soffice",
                               NULL, "mocka-dock-1_TIME0", launches, &step);
  g_assert_nonnull (entry);
  g_assert_cmpstr (entry->id, ==, "libreoffice-writer.desktop");
  g_assert_cmpint (step, ==, MOCKA_MATCH_STARTUP_ID);
  g_clear_pointer (&entry, mocka_app_entry_unref);

  /* Launched by another program: not in the table. */
  entry = mocka_matcher_match (fixture_index, "soffice", "Soffice",
                               NULL, "brisk-menu-1_TIME0", launches, &step);
  g_assert_null (entry);
  g_assert_cmpint (step, ==, MOCKA_MATCH_NONE);
}

static void
assert_executable_match (MockaAppIndex  *index,
                         const gchar    *executable,
                         const gchar    *expect)
{
  g_autoptr(MockaAppEntry) entry = NULL;
  MockaMatchStep step;

  /* Window names that match nothing, as for a menu editor's entry. */
  entry = mocka_matcher_match (index, "unknown-app", "Unknown-app", executable,
                               NULL, NULL, &step);
  if (expect == NULL)
    {
      g_assert_null (entry);
      return;
    }

  g_assert_nonnull (entry);
  g_assert_cmpstr (entry->id, ==, expect);
  g_assert_cmpint (step, ==, MOCKA_MATCH_EXECUTABLE);
}

/* Step 5: an entry giving a path matches the process running that file. */
static void
test_executable_path (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "editor-made.desktop",
               "Exec='/opt/ide-1.0/bin/ide' %f\n");
  write_entry (dir, "other.desktop", "Exec=/opt/other/bin/ide\n");
  index = index_for (dir);
  assert_executable_match (index, "/opt/ide-1.0/bin/ide", "editor-made.desktop");
  assert_executable_match (index, "/opt/ide-2.0/bin/ide", NULL);

  remove_tree (dir);
}

/* Step 5: an entry giving a bare name matches any process of that name. */
static void
test_executable_name (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "tool.desktop", "Exec=tool %f\n");
  index = index_for (dir);
  assert_executable_match (index, "/home/user/tool-3/bin/tool", "tool.desktop");

  remove_tree (dir);
}

/* Step 5 leaves out entries running an interpreter with a script. */
static void
test_executable_interpreter (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "script.desktop",
               "Exec=/usr/local/bin/python3 /usr/local/share/script/main.py\n");
  write_entry (dir, "python.desktop", "Exec=python3 /opt/other.py\n");
  index = index_for (dir);
  assert_executable_match (index, "/usr/local/bin/python3", NULL);

  remove_tree (dir);
}

/* Step 5 also resolves symbolic links, as for /home and /usr/home. */
static void
test_executable_link (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autofree gchar *real = g_build_filename (dir, "real-tool", NULL);
  g_autofree gchar *link = g_build_filename (dir, "link-tool", NULL);
  g_autofree gchar *keys = g_strdup_printf ("Exec=%s\n", link);
  g_autoptr(MockaAppIndex) index = NULL;
  g_autofree gchar *resolved = NULL;

  g_assert_true (g_file_set_contents (real, "", -1, NULL));
  g_assert_cmpint (symlink (real, link), ==, 0);
  write_entry (dir, "linked.desktop", keys);
  index = index_for (dir);

  /* The kernel reports the resolved path of the running file. */
  resolved = mocka_resolve_path (real);
  assert_executable_match (index, resolved, "linked.desktop");

  g_remove (link);
  g_remove (real);
  remove_tree (dir);
}

/* The first directory wins for an ID, and a Hidden entry deletes it. */
static void
test_precedence (void)
{
  g_autofree gchar *high = make_dir ();
  g_autofree gchar *low = make_dir ();
  const gchar *dirs[] = { high, low, NULL };
  g_autoptr(MockaAppIndex) index = NULL;
  MockaAppEntry *entry;

  write_entry (high, "app.desktop", "Exec=app-high\n");
  write_entry (low, "app.desktop", "Exec=app-low\n");
  write_entry (high, "gone.desktop", "Exec=gone\nHidden=true\n");
  write_entry (low, "gone.desktop", "Exec=gone\n");
  index = mocka_app_index_new (dirs);

  entry = mocka_app_index_lookup (index, "app.desktop");
  g_assert_nonnull (entry);
  g_assert_cmpstr (entry->program, ==, "app-high");
  g_assert_null (mocka_app_index_lookup (index, "gone.desktop"));

  remove_tree (high);
  remove_tree (low);
}

/* Files in subdirectories get "-" separated IDs. */
static void
test_subdir_id (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;

  write_entry (dir, "wine/Programs/notepad.desktop",
               "Exec=wine notepad.exe\nStartupWMClass=notepad.exe\n");
  index = index_for (dir);
  assert_match (index, "notepad.exe", "notepad.exe",
                "wine-Programs-notepad.desktop",
                MOCKA_MATCH_STARTUP_WM_CLASS_INSTANCE);

  remove_tree (dir);
}

/* Entries that are not applications are ignored. */
static void
test_not_application (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autofree gchar *path = g_build_filename (dir, "link.desktop", NULL);
  g_autoptr(MockaAppIndex) index = NULL;

  g_assert_true (g_file_set_contents (path,
      "[Desktop Entry]\nType=Link\nName=Link\nURL=https://example.org\n",
      -1, NULL));
  index = index_for (dir);
  g_assert_null (mocka_app_index_lookup (index, "link.desktop"));

  remove_tree (dir);
}

static void
on_changed (MockaAppIndex *index,
            gpointer       user_data)
{
  (*(guint *) user_data)++;
}

/* Reload picks up new entries and announces the change. */
static void
test_reload (void)
{
  g_autofree gchar *dir = make_dir ();
  g_autoptr(MockaAppIndex) index = NULL;
  guint changed = 0;

  index = index_for (dir);
  g_signal_connect (index, "changed", G_CALLBACK (on_changed), &changed);
  g_assert_null (mocka_app_index_lookup (index, "new.desktop"));

  write_entry (dir, "new.desktop", "Exec=new\n");
  mocka_app_index_reload (index);
  g_assert_nonnull (mocka_app_index_lookup (index, "new.desktop"));
  g_assert_cmpuint (changed, ==, 1);

  remove_tree (dir);
}

static gint
compare_names (gconstpointer a,
               gconstpointer b)
{
  return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

int
main (int    argc,
      char **argv)
{
  const gchar *dirs[] = { TEST_DATA_DIR "/applications", NULL };
  g_autoptr(GDir) data_dir = NULL;
  g_autoptr(GPtrArray) samples = g_ptr_array_new ();
  const gchar *name;
  guint i;
  int ret;

  g_test_init (&argc, &argv, NULL);

  fixture_index = mocka_app_index_new (dirs);

  data_dir = g_dir_open (TEST_DATA_DIR, 0, NULL);
  g_assert_nonnull (data_dir);
  while ((name = g_dir_read_name (data_dir)) != NULL)
    if (g_str_has_suffix (name, ".txt"))
      g_ptr_array_add (samples, g_strdup (name));
  g_ptr_array_sort (samples, compare_names);
  g_assert_cmpuint (samples->len, >, 0);

  for (i = 0; i < samples->len; i++)
    {
      g_autofree gchar *test_path = NULL;

      name = g_ptr_array_index (samples, i);
      test_path = g_strdup_printf ("/matcher/sample/%s", name);
      g_test_add_data_func_full (test_path, (gpointer) name, test_sample, g_free);
    }

  g_test_add_func ("/matcher/hidden-loses", test_hidden_loses);
  g_test_add_func ("/matcher/hidden-fallback", test_hidden_fallback);
  g_test_add_func ("/matcher/tie-break", test_tie_break);
  g_test_add_func ("/matcher/try-exec", test_try_exec);
  g_test_add_func ("/matcher/instance-first", test_instance_first);
  g_test_add_func ("/matcher/startup-id", test_startup_id);
  g_test_add_func ("/matcher/executable-path", test_executable_path);
  g_test_add_func ("/matcher/executable-name", test_executable_name);
  g_test_add_func ("/matcher/executable-interpreter", test_executable_interpreter);
  g_test_add_func ("/matcher/executable-link", test_executable_link);
  g_test_add_func ("/app-index/precedence", test_precedence);
  g_test_add_func ("/app-index/subdir-id", test_subdir_id);
  g_test_add_func ("/app-index/not-application", test_not_application);
  g_test_add_func ("/app-index/reload", test_reload);

  ret = g_test_run ();

  g_object_unref (fixture_index);
  return ret;
}