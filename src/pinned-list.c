/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * Edits of the ordered pinned-apps list (SPEC section 10). Each returns a
 * new list and leaves the given one untouched.
 */

#include "config.h"

#include "pinned-list.h"

/*
 * Pins id at a gap of the list: 0 is before the first app, and the number
 * of apps is after the last. A gap past the end means the end. When id is
 * already pinned it moves there, so dragging a pinned app reorders it.
 */
gchar **
mocka_pinned_list_insert (const gchar * const *ids,
                          const gchar         *id,
                          guint                gap)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  gboolean added = FALSE;
  guint i;

  g_return_val_if_fail (ids != NULL, NULL);
  g_return_val_if_fail (id != NULL, NULL);

  for (i = 0; ids[i] != NULL; i++)
    {
      if (i == gap)
        {
          g_strv_builder_add (builder, id);
          added = TRUE;
        }
      if (!g_str_equal (ids[i], id))
        g_strv_builder_add (builder, ids[i]);
    }

  if (!added)
    g_strv_builder_add (builder, id);

  return g_strv_builder_end (builder);
}

/*
 * Unpins id. position, when not NULL, is set to where it was, for Undo, or
 * to the length of the list when it was not pinned.
 */
gchar **
mocka_pinned_list_remove (const gchar * const *ids,
                          const gchar         *id,
                          guint               *position)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  guint i;
  guint found = G_MAXUINT;

  g_return_val_if_fail (ids != NULL, NULL);
  g_return_val_if_fail (id != NULL, NULL);

  for (i = 0; ids[i] != NULL; i++)
    {
      if (g_str_equal (ids[i], id))
        found = MIN (found, i);
      else
        g_strv_builder_add (builder, ids[i]);
    }

  if (position != NULL)
    *position = found != G_MAXUINT ? found : i;

  return g_strv_builder_end (builder);
}