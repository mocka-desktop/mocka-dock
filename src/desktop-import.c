/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Desktop entry files dropped on the dock (SPEC section 10). A file already
 * in an applications directory keeps its desktop entry ID. Any other file
 * has no ID, so it is copied into the user's applications directory and the
 * copy is used.
 */

#include "config.h"

#include "desktop-import.h"

#include <errno.h>
#include <string.h>

#include <gio/gio.h>

#include "app-index.h"

/*
 * The desktop entry ID of path when it lies under dir: its path relative
 * to dir with "/" replaced by "-" (Desktop Entry specification). NULL when
 * it lies elsewhere.
 */
static gchar *
id_in_dir (const gchar *path,
           const gchar *dir)
{
  g_autofree gchar *resolved_dir = mocka_resolve_path (dir);
  gsize length = strlen (resolved_dir);
  gchar *id;

  if (strncmp (path, resolved_dir, length) != 0 || path[length] != '/')
    return NULL;

  id = g_strdup (path + length + 1);
  g_strdelimit (id, "/", '-');
  return id;
}

/* Only application entries can be pinned. */
static gboolean
check_application (const gchar  *path,
                   GError      **error)
{
  g_autoptr(GKeyFile) file = g_key_file_new ();
  g_autofree gchar *type = NULL;

  if (!g_str_has_suffix (path, ".desktop"))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "%s is not a desktop entry", path);
      return FALSE;
    }

  if (!g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, error))
    return FALSE;

  type = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP,
                                G_KEY_FILE_DESKTOP_KEY_TYPE, NULL);
  if (g_strcmp0 (type, G_KEY_FILE_DESKTOP_TYPE_APPLICATION) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "%s is not an application", path);
      return FALSE;
    }

  return TRUE;
}

/*
 * Returns the desktop entry ID to pin for the dropped file at path.
 * app_dirs are the applications directories, highest precedence first;
 * user_dir is the user's own, where other files are copied. A file with the
 * same name and contents already there is reused; otherwise a free name is
 * picked, such as app-2.desktop.
 */
gchar *
mocka_desktop_import (const gchar         *path,
                      const gchar * const *app_dirs,
                      const gchar         *user_dir,
                      GError             **error)
{
  g_autofree gchar *resolved = NULL;
  g_autofree gchar *contents = NULL;
  g_autofree gchar *base = NULL;
  gsize length;
  guint i;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (user_dir != NULL, NULL);

  if (!check_application (path, error))
    return NULL;

  resolved = mocka_resolve_path (path);
  for (i = 0; app_dirs != NULL && app_dirs[i] != NULL; i++)
    {
      gchar *id = id_in_dir (resolved, app_dirs[i]);

      if (id != NULL)
        return id;
    }

  if (!g_file_get_contents (path, &contents, &length, error))
    return NULL;

  if (g_mkdir_with_parents (user_dir, 0700) != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Cannot create %s", user_dir);
      return NULL;
    }

  base = g_path_get_basename (path);
  base[strlen (base) - strlen (".desktop")] = '\0';

  for (i = 1; i < 1000; i++)
    {
      g_autofree gchar *name = i == 1 ? g_strconcat (base, ".desktop", NULL)
                                      : g_strdup_printf ("%s-%u.desktop", base, i);
      g_autofree gchar *target = g_build_filename (user_dir, name, NULL);
      g_autofree gchar *existing = NULL;
      gsize existing_length;

      if (g_file_get_contents (target, &existing, &existing_length, NULL))
        {
          if (existing_length == length && memcmp (existing, contents, length) == 0)
            return g_steal_pointer (&name);
          continue;
        }

      if (!g_file_set_contents (target, contents, length, error))
        return NULL;
      return g_steal_pointer (&name);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
               "No free name for %s in %s", base, user_dir);
  return NULL;
}
