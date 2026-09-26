/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Startup notification messages (freedesktop.org Startup Notification
 * specification), such as:
 *
 *   remove: ID="launcher-123_TIME4567"
 *
 * A message is a type, a colon, then KEY=VALUE pairs separated by spaces.
 * A value is either quoted, with \" and \\ escapes inside, or unquoted,
 * with a backslash escaping the next character.
 */

#include "config.h"

#include "startup-message.h"

#include <string.h>

/* Reads one value at *p, advancing past it. */
static gchar *
read_value (const gchar **p)
{
  GString *value = g_string_new (NULL);
  gboolean quoted = FALSE;

  while (**p != '\0' && (quoted || **p != ' '))
    {
      if (**p == '"')
        {
          quoted = !quoted;
        }
      else if (**p == '\\' && (*p)[1] != '\0')
        {
          (*p)++;
          g_string_append_c (value, **p);
        }
      else
        {
          g_string_append_c (value, **p);
        }
      (*p)++;
    }

  return g_string_free (value, FALSE);
}

/*
 * Returns the value of key in a message of this type, such as "remove"
 * and "ID", or NULL when the message is of another type or has no such
 * key.
 */
gchar *
mocka_startup_message_get_value (const gchar *message,
                                 const gchar *type,
                                 const gchar *key)
{
  gsize type_length = strlen (type);
  gsize key_length = strlen (key);
  const gchar *p;

  g_return_val_if_fail (message != NULL, NULL);

  if (strncmp (message, type, type_length) != 0 || message[type_length] != ':')
    return NULL;

  p = message + type_length + 1;
  while (*p != '\0')
    {
      g_autofree gchar *value = NULL;
      gboolean wanted;

      while (*p == ' ')
        p++;
      if (*p == '\0')
        break;

      wanted = strncmp (p, key, key_length) == 0 && p[key_length] == '=';
      while (*p != '\0' && *p != '=' && *p != ' ')
        p++;
      if (*p != '=')
        continue;
      p++;

      value = read_value (&p);
      if (wanted)
        return g_steal_pointer (&value);
    }

  return NULL;
}
