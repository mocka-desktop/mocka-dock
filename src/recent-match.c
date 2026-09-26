/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Which recent files belong to an app (SPEC section 9.1). Each recent file
 * lists the applications that used it, as a name and a command line. The
 * name is often not the app's (a file manager records itself when it opens
 * a file with another app), so the command's program is compared first.
 */

#include "recent-match.h"

#include <string.h>

/* The program file name of a recorded command line, which may be quoted. */
static gchar *
exec_program (const gchar *exec)
{
  g_auto(GStrv) argv = NULL;

  if (!g_shell_parse_argv (exec, NULL, &argv, NULL))
    return NULL;

  /* Some files hold the whole command as one quoted word. */
  if (strchr (argv[0], ' ') != NULL)
    {
      g_auto(GStrv) inner = NULL;

      if (!g_shell_parse_argv (argv[0], NULL, &inner, NULL))
        return NULL;
      return g_path_get_basename (inner[0]);
    }

  return g_path_get_basename (argv[0]);
}

static gboolean
casefold_equal (const gchar *a,
                const gchar *b)
{
  g_autofree gchar *fa = g_utf8_casefold (a, -1);
  g_autofree gchar *fb = g_utf8_casefold (b, -1);

  return g_str_equal (fa, fb);
}

/*
 * An application recorded on a recent file is this app when its command
 * runs the app's program, or when its name is the app's desktop entry ID
 * without ".desktop". Case is ignored.
 */
gboolean
mocka_recent_app_matches (MockaAppEntry *entry,
                          const gchar   *app_name,
                          const gchar   *app_exec)
{
  g_return_val_if_fail (entry != NULL, FALSE);

  if (app_exec != NULL && entry->program != NULL)
    {
      g_autofree gchar *program = exec_program (app_exec);

      if (program != NULL && casefold_equal (program, entry->program))
        return TRUE;
    }

  if (app_name != NULL && g_str_has_suffix (entry->id, ".desktop"))
    {
      g_autofree gchar *id = g_strndup (entry->id,
                                        strlen (entry->id) - strlen (".desktop"));

      if (casefold_equal (app_name, id))
        return TRUE;
    }

  return FALSE;
}