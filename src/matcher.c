/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/* Window-to-app matching chain (SPEC section 6). */

#include "config.h"

#include "matcher.h"

/* First entry in tie-break order that is visible, or hidden on the second pass. */
static MockaAppEntry *
pick (GPtrArray *candidates,
      gboolean   hidden)
{
  guint i;

  for (i = 0; candidates != NULL && i < candidates->len; i++)
    {
      MockaAppEntry *entry = g_ptr_array_index (candidates, i);

      if (entry->no_display == hidden)
        return entry;
    }

  return NULL;
}

/* Steps 1 to 4 against either the visible or the hidden entries. */
static MockaAppEntry *
match_names (MockaAppIndex  *index,
             const gchar    *instance,
             const gchar    *res_class,
             gboolean        hidden,
             MockaMatchStep *step)
{
  const gchar *names[] = { instance, res_class };
  MockaAppEntry *entry;
  guint i;

  entry = pick (mocka_app_index_find (index, MOCKA_APP_KEY_STARTUP_WM_CLASS,
                                      instance), hidden);
  if (entry != NULL)
    {
      *step = MOCKA_MATCH_STARTUP_WM_CLASS_INSTANCE;
      return entry;
    }

  entry = pick (mocka_app_index_find (index, MOCKA_APP_KEY_STARTUP_WM_CLASS,
                                      res_class), hidden);
  if (entry != NULL)
    {
      *step = MOCKA_MATCH_STARTUP_WM_CLASS_CLASS;
      return entry;
    }

  for (i = 0; i < G_N_ELEMENTS (names); i++)
    {
      entry = pick (mocka_app_index_find (index, MOCKA_APP_KEY_ID, names[i]),
                    hidden);
      if (entry != NULL)
        {
          *step = MOCKA_MATCH_ID;
          return entry;
        }
    }

  for (i = 0; i < G_N_ELEMENTS (names); i++)
    {
      entry = pick (mocka_app_index_find (index, MOCKA_APP_KEY_PROGRAM,
                                          names[i]), hidden);
      if (entry != NULL)
        {
          *step = MOCKA_MATCH_PROGRAM;
          return entry;
        }
    }

  return NULL;
}

/*
 * Finds the app of a window from its WM_CLASS instance and class names and
 * its startup notification ID (read from the window or its client leader).
 * launches maps the startup IDs of apps the dock launched to their desktop
 * entry IDs, and may be NULL.
 *
 * Returns a new reference to the entry, or NULL when the window must use the
 * class fallback (step 6). step, when not NULL, is set to the step that
 * decided.
 */
MockaAppEntry *
mocka_matcher_match (MockaAppIndex  *index,
                     const gchar    *instance,
                     const gchar    *res_class,
                     const gchar    *startup_id,
                     GHashTable     *launches,
                     MockaMatchStep *step)
{
  MockaMatchStep matched = MOCKA_MATCH_NONE;
  MockaAppEntry *entry;
  const gchar *id;

  g_return_val_if_fail (MOCKA_IS_APP_INDEX (index), NULL);

  /* Visible entries first; hidden ones only when no visible entry matches. */
  entry = match_names (index, instance, res_class, FALSE, &matched);
  if (entry == NULL)
    entry = match_names (index, instance, res_class, TRUE, &matched);

  if (entry == NULL && startup_id != NULL && launches != NULL)
    {
      id = g_hash_table_lookup (launches, startup_id);
      if (id != NULL)
        {
          entry = mocka_app_index_lookup (index, id);
          if (entry != NULL)
            matched = MOCKA_MATCH_STARTUP_ID;
        }
    }

  if (step != NULL)
    *step = entry != NULL ? matched : MOCKA_MATCH_NONE;

  return entry != NULL ? mocka_app_entry_ref (entry) : NULL;
}