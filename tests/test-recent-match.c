/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Recent files of an app (SPEC section 9.1), with applications as they are
 * recorded in recently-used.xbel on GhostBSD.
 */

#include <glib.h>

#include "recent-match.h"

static MockaAppEntry *
entry_new (const gchar *id,
           const gchar *program)
{
  MockaAppEntry *entry = g_new0 (MockaAppEntry, 1);

  entry->id = g_strdup (id);
  entry->program = g_strdup (program);

  return entry;
}

static void
entry_free (MockaAppEntry *entry)
{
  g_free (entry->id);
  g_free (entry->program);
  g_free (entry);
}

static void
test_program (void)
{
  MockaAppEntry *pluma = entry_new ("pluma.desktop", "pluma");

  g_assert_true (mocka_recent_app_matches (pluma, "Pluma", "'pluma %u'"));
  g_assert_true (mocka_recent_app_matches (pluma, "Pluma", "pluma %u"));
  g_assert_true (mocka_recent_app_matches (pluma, "x", "/usr/local/bin/pluma %u"));
  g_assert_false (mocka_recent_app_matches (pluma, "Firefox", "'firefox %u'"));

  entry_free (pluma);
}

/* A file manager records itself with the command of the app it opened. */
static void
test_opened_by_file_manager (void)
{
  MockaAppEntry *inkscape = entry_new ("org.inkscape.Inkscape.desktop", "inkscape");
  MockaAppEntry *caja = entry_new ("caja-browser.desktop", "caja");

  g_assert_true (mocka_recent_app_matches (inkscape, "Caja", "'inkscape %F'"));
  g_assert_false (mocka_recent_app_matches (caja, "Caja", "'inkscape %F'"));

  entry_free (inkscape);
  entry_free (caja);
}

/* Some apps record their ID as their name. */
static void
test_name_is_id (void)
{
  MockaAppEntry *inkscape = entry_new ("org.inkscape.Inkscape.desktop", "inkscape");

  g_assert_true (mocka_recent_app_matches (inkscape, "org.inkscape.Inkscape",
                                           "'org.inkscape.Inkscape %u'"));
  g_assert_true (mocka_recent_app_matches (inkscape, "org.inkscape.Inkscape", NULL));

  entry_free (inkscape);
}

static void
test_invalid (void)
{
  MockaAppEntry *pluma = entry_new ("pluma.desktop", "pluma");
  MockaAppEntry *no_program = entry_new ("x.desktop", NULL);

  g_assert_false (mocka_recent_app_matches (pluma, NULL, NULL));
  g_assert_false (mocka_recent_app_matches (pluma, "Other", "'unterminated"));
  g_assert_false (mocka_recent_app_matches (pluma, "Other", ""));
  g_assert_false (mocka_recent_app_matches (no_program, "Pluma", "'pluma %u'"));

  entry_free (pluma);
  entry_free (no_program);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/recent-match/program", test_program);
  g_test_add_func ("/recent-match/opened-by-file-manager", test_opened_by_file_manager);
  g_test_add_func ("/recent-match/name-is-id", test_name_is_id);
  g_test_add_func ("/recent-match/invalid", test_invalid);

  return g_test_run ();
}