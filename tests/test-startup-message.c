/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/* Startup notification messages, for the launch pulse (SPEC section 7). */

#include <glib.h>

#include "startup-message.h"

static void
assert_value (const gchar *message,
              const gchar *type,
              const gchar *key,
              const gchar *expected)
{
  g_autofree gchar *value = mocka_startup_message_get_value (message, type, key);

  g_assert_cmpstr (value, ==, expected);
}

static void
test_remove (void)
{
  assert_value ("remove: ID=launcher-1_TIME5", "remove", "ID", "launcher-1_TIME5");
  assert_value ("remove: ID=\"launcher-1_TIME5\"", "remove", "ID", "launcher-1_TIME5");
}

/* The key is found among others, and a similar key does not count. */
static void
test_keys (void)
{
  const gchar *message = "new: NAME=\"Text Editor\" SCREEN=0 BIN=pluma "
                         "ICON=accessories-text-editor ID=gtk-launch-9_TIME0";

  assert_value (message, "new", "ID", "gtk-launch-9_TIME0");
  assert_value (message, "new", "NAME", "Text Editor");
  assert_value (message, "new", "SCREEN", "0");
  assert_value ("remove: XID=5 ID=a", "remove", "ID", "a");
  assert_value ("remove: XID=5", "remove", "ID", NULL);
}

static void
test_escapes (void)
{
  assert_value ("remove: ID=\"a \\\"b\\\" c\\\\d\"", "remove", "ID", "a \"b\" c\\d");
  assert_value ("remove: ID=a\\ b", "remove", "ID", "a b");
}

/* Only messages of the asked type count. */
static void
test_type (void)
{
  assert_value ("new: ID=a", "remove", "ID", NULL);
  assert_value ("removed: ID=a", "remove", "ID", NULL);
  assert_value ("remove:", "remove", "ID", NULL);
  assert_value ("", "remove", "ID", NULL);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/startup-message/remove", test_remove);
  g_test_add_func ("/startup-message/keys", test_keys);
  g_test_add_func ("/startup-message/escapes", test_escapes);
  g_test_add_func ("/startup-message/type", test_type);

  return g_test_run ();
}
