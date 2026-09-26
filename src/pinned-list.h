/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

gchar **mocka_pinned_list_insert (const gchar * const *ids,
                                  const gchar         *id,
                                  guint                gap);
gchar **mocka_pinned_list_remove (const gchar * const *ids,
                                  const gchar         *id,
                                  guint               *position);

G_END_DECLS