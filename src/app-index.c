/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Index of the installed application desktop entries (freedesktop.org
 * Desktop Entry specification), with the lookup tables the matcher needs.
 * The index is built once and rebuilt only when installed applications
 * change.
 */

#include "config.h"

#include "app-index.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct _MockaAppIndex
{
  GObject parent_instance;

  gchar **dirs;           /* applications directories, highest precedence first */
  GPtrArray *entries;     /* MockaAppEntry, owned */
  GHashTable *by_id;      /* ID → MockaAppEntry */
  GHashTable *tables[5];  /* MockaAppKey → (key → GPtrArray of MockaAppEntry) */
  GAppInfoMonitor *monitor;
};

enum
{
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (MockaAppIndex, mocka_app_index, G_TYPE_OBJECT)

static void
mocka_app_entry_clear (MockaAppEntry *entry)
{
  g_free (entry->id);
  g_free (entry->path);
  g_free (entry->name);
  g_free (entry->icon);
  g_free (entry->exec);
  g_free (entry->startup_wm_class);
  g_free (entry->program);
  g_free (entry->program_path);
}

MockaAppEntry *
mocka_app_entry_ref (MockaAppEntry *entry)
{
  return g_rc_box_acquire (entry);
}

void
mocka_app_entry_unref (MockaAppEntry *entry)
{
  g_rc_box_release_full (entry, (GDestroyNotify) mocka_app_entry_clear);
}

/* A field code is "%" followed by one letter, such as %U or %f. */
static gboolean
is_field_code (const gchar *arg)
{
  return arg[0] == '%' && g_ascii_isalpha (arg[1]) && arg[2] == '\0';
}

/*
 * Returns NULL for files that are not applications, and for entries marked
 * Hidden, which the Desktop Entry specification treats as deleted.
 */
static MockaAppEntry *
load_entry (const gchar *path,
            const gchar *id)
{
  g_autoptr(GKeyFile) file = g_key_file_new ();
  g_autofree gchar *type = NULL;
  g_autofree gchar *try_exec = NULL;
  g_auto(GStrv) argv = NULL;
  MockaAppEntry *entry;
  const gchar *group = G_KEY_FILE_DESKTOP_GROUP;
  int i;

  if (!g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, NULL))
    return NULL;

  type = g_key_file_get_string (file, group, G_KEY_FILE_DESKTOP_KEY_TYPE, NULL);
  if (g_strcmp0 (type, G_KEY_FILE_DESKTOP_TYPE_APPLICATION) != 0)
    return NULL;

  if (g_key_file_get_boolean (file, group, G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL))
    return NULL;

  entry = g_rc_box_new0 (MockaAppEntry);
  entry->id = g_strdup (id);
  entry->path = g_strdup (path);
  entry->name = g_key_file_get_locale_string (file, group,
      G_KEY_FILE_DESKTOP_KEY_NAME, NULL, NULL);
  entry->icon = g_key_file_get_string (file, group,
      G_KEY_FILE_DESKTOP_KEY_ICON, NULL);
  entry->exec = g_key_file_get_string (file, group,
      G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
  entry->startup_wm_class = g_key_file_get_string (file, group,
      G_KEY_FILE_DESKTOP_KEY_STARTUP_WM_CLASS, NULL);
  entry->no_display = g_key_file_get_boolean (file, group,
      G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL);

  if (entry->exec != NULL)
    g_shell_parse_argv (entry->exec, NULL, &argv, NULL);

  for (i = 1; argv != NULL && argv[i] != NULL; i++)
    {
      if (!is_field_code (argv[i]))
        {
          entry->exec_has_args = TRUE;
          break;
        }
    }

  try_exec = g_key_file_get_string (file, group,
      G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, NULL);
  if (try_exec != NULL && *try_exec != '\0')
    entry->program_path = g_steal_pointer (&try_exec);
  else if (argv != NULL && argv[0] != NULL)
    entry->program_path = g_strdup (argv[0]);

  if (entry->program_path != NULL)
    entry->program = g_path_get_basename (entry->program_path);

  return entry;
}

/*
 * A path with symbolic links resolved, such as /usr/home/... for /home/...
 * on FreeBSD. A path that does not exist is only made absolute and tidied.
 */
gchar *
mocka_resolve_path (const gchar *path)
{
  char resolved[PATH_MAX];

  if (realpath (path, resolved) != NULL)
    return g_strdup (resolved);

  return g_canonicalize_filename (path, "/");
}

/*
 * Reads one applications directory. Files in subdirectories get IDs with
 * "/" replaced by "-", as the Desktop Entry specification defines. An ID
 * already seen in a directory of higher precedence is skipped, including
 * when that entry was Hidden.
 */
static void
add_dir (MockaAppIndex *self,
         GHashTable    *seen,
         const gchar   *dir,
         const gchar   *prefix)
{
  g_autoptr(GDir) handle = g_dir_open (dir, 0, NULL);
  const gchar *name;

  if (handle == NULL)
    return;

  while ((name = g_dir_read_name (handle)) != NULL)
    {
      g_autofree gchar *path = g_build_filename (dir, name, NULL);
      g_autofree gchar *id = g_strconcat (prefix, name, NULL);
      MockaAppEntry *entry;

      if (g_file_test (path, G_FILE_TEST_IS_DIR))
        {
          g_autofree gchar *sub_prefix = g_strconcat (id, "-", NULL);

          add_dir (self, seen, path, sub_prefix);
          continue;
        }

      if (!g_str_has_suffix (name, ".desktop")
          || g_hash_table_contains (seen, id))
        continue;

      g_hash_table_add (seen, g_strdup (id));

      entry = load_entry (path, id);
      if (entry == NULL)
        continue;

      g_ptr_array_add (self->entries, entry);
      g_hash_table_insert (self->by_id, entry->id, entry);
    }
}

static void
table_add (GHashTable    *table,
           gchar         *key,
           MockaAppEntry *entry)
{
  GPtrArray *list = g_hash_table_lookup (table, key);

  if (list == NULL)
    {
      list = g_ptr_array_new ();
      g_hash_table_insert (table, key, list);
    }
  else
    {
      g_free (key);
    }

  g_ptr_array_add (list, entry);
}

/*
 * Tie-break when several entries match in the same step: an Exec without
 * arguments besides field codes first, then by ID.
 */
static gint
compare_entries (gconstpointer a,
                 gconstpointer b)
{
  const MockaAppEntry *ea = *(MockaAppEntry * const *) a;
  const MockaAppEntry *eb = *(MockaAppEntry * const *) b;

  if (ea->exec_has_args != eb->exec_has_args)
    return ea->exec_has_args ? 1 : -1;

  return strcmp (ea->id, eb->id);
}

static void
build_tables (MockaAppIndex *self)
{
  GHashTableIter iter;
  gpointer list;
  guint i, t;

  for (i = 0; i < self->entries->len; i++)
    {
      MockaAppEntry *entry = g_ptr_array_index (self->entries, i);
      gsize id_len = strlen (entry->id);

      if (entry->startup_wm_class != NULL)
        table_add (self->tables[MOCKA_APP_KEY_STARTUP_WM_CLASS],
                   g_strdup (entry->startup_wm_class), entry);

      table_add (self->tables[MOCKA_APP_KEY_ID],
                 g_utf8_casefold (entry->id, id_len - strlen (".desktop")),
                 entry);

      if (entry->program != NULL)
        table_add (self->tables[MOCKA_APP_KEY_PROGRAM],
                   g_utf8_casefold (entry->program, -1), entry);

      /* Step 5 leaves out interpreters run with a script. */
      if (entry->program_path != NULL && !entry->exec_has_args)
        {
          if (strchr (entry->program_path, '/') != NULL)
            table_add (self->tables[MOCKA_APP_KEY_EXECUTABLE],
                       mocka_resolve_path (entry->program_path), entry);
          else
            table_add (self->tables[MOCKA_APP_KEY_EXECUTABLE_NAME],
                       g_strdup (entry->program_path), entry);
        }
    }

  for (t = 0; t < G_N_ELEMENTS (self->tables); t++)
    {
      g_hash_table_iter_init (&iter, self->tables[t]);
      while (g_hash_table_iter_next (&iter, NULL, &list))
        g_ptr_array_sort (list, compare_entries);
    }
}

void
mocka_app_index_reload (MockaAppIndex *self)
{
  g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, NULL);
  guint i;

  g_return_if_fail (MOCKA_IS_APP_INDEX (self));

  for (i = 0; i < G_N_ELEMENTS (self->tables); i++)
    g_hash_table_remove_all (self->tables[i]);
  g_hash_table_remove_all (self->by_id);
  g_ptr_array_set_size (self->entries, 0);

  for (i = 0; self->dirs[i] != NULL; i++)
    add_dir (self, seen, self->dirs[i], "");

  build_tables (self);

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/*
 * Returns the entry with this desktop entry ID, or NULL. The entry stays
 * valid until the next reload; take a reference to keep it longer.
 */
MockaAppEntry *
mocka_app_index_lookup (MockaAppIndex *self,
                        const gchar   *id)
{
  g_return_val_if_fail (MOCKA_IS_APP_INDEX (self), NULL);
  g_return_val_if_fail (id != NULL, NULL);

  return g_hash_table_lookup (self->by_id, id);
}

/*
 * Returns the entries matching value for this key, in tie-break order, or
 * NULL. The array is owned by the index and valid until the next reload.
 */
GPtrArray *
mocka_app_index_find (MockaAppIndex *self,
                      MockaAppKey    key,
                      const gchar   *value)
{
  g_autofree gchar *folded = NULL;

  g_return_val_if_fail (MOCKA_IS_APP_INDEX (self), NULL);
  g_return_val_if_fail (key < G_N_ELEMENTS (self->tables), NULL);

  if (value == NULL)
    return NULL;

  if (key == MOCKA_APP_KEY_STARTUP_WM_CLASS
      || key == MOCKA_APP_KEY_EXECUTABLE
      || key == MOCKA_APP_KEY_EXECUTABLE_NAME)
    return g_hash_table_lookup (self->tables[key], value);

  folded = g_utf8_casefold (value, -1);
  return g_hash_table_lookup (self->tables[key], folded);
}

static void
on_apps_changed (GAppInfoMonitor *monitor,
                 gpointer         user_data)
{
  mocka_app_index_reload (MOCKA_APP_INDEX (user_data));
}

static void
mocka_app_index_finalize (GObject *object)
{
  MockaAppIndex *self = MOCKA_APP_INDEX (object);
  guint i;

  if (self->monitor != NULL)
    g_signal_handlers_disconnect_by_data (self->monitor, self);
  g_clear_object (&self->monitor);

  for (i = 0; i < G_N_ELEMENTS (self->tables); i++)
    g_hash_table_unref (self->tables[i]);
  g_hash_table_unref (self->by_id);
  g_ptr_array_unref (self->entries);
  g_strfreev (self->dirs);

  G_OBJECT_CLASS (mocka_app_index_parent_class)->finalize (object);
}

static void
mocka_app_index_class_init (MockaAppIndexClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mocka_app_index_finalize;

  signals[SIGNAL_CHANGED] =
    g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
mocka_app_index_init (MockaAppIndex *self)
{
  guint i;

  self->entries = g_ptr_array_new_with_free_func (
      (GDestroyNotify) mocka_app_entry_unref);
  self->by_id = g_hash_table_new (g_str_hash, g_str_equal);

  for (i = 0; i < G_N_ELEMENTS (self->tables); i++)
    self->tables[i] = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                             (GDestroyNotify) g_ptr_array_unref);
}

/*
 * Creates an index of the given applications directories, highest
 * precedence first, and loads it.
 */
MockaAppIndex *
mocka_app_index_new (const gchar * const *dirs)
{
  MockaAppIndex *self = g_object_new (MOCKA_TYPE_APP_INDEX, NULL);

  self->dirs = g_strdupv ((gchar **) dirs);
  mocka_app_index_reload (self);

  return self;
}

/*
 * Creates an index of the XDG applications directories that reloads itself
 * when installed applications change.
 */
MockaAppIndex *
mocka_app_index_new_for_system (void)
{
  const gchar * const *data_dirs = g_get_system_data_dirs ();
  g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
  MockaAppIndex *self;
  guint i;

  g_ptr_array_add (dirs, g_build_filename (g_get_user_data_dir (),
                                           "applications", NULL));
  for (i = 0; data_dirs[i] != NULL; i++)
    g_ptr_array_add (dirs, g_build_filename (data_dirs[i], "applications", NULL));
  g_ptr_array_add (dirs, NULL);

  self = mocka_app_index_new ((const gchar * const *) dirs->pdata);

  /* GAppInfoMonitor only reports changes once GIO has listed the apps. */
  self->monitor = g_app_info_monitor_get ();
  g_list_free_full (g_app_info_get_all (), g_object_unref);
  g_signal_connect (self->monitor, "changed",
                    G_CALLBACK (on_apps_changed), self);

  return self;
}