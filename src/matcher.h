/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include "app-index.h"

G_BEGIN_DECLS

/* The step of SPEC section 6 that matched. */
typedef enum
{
  MOCKA_MATCH_STARTUP_WM_CLASS_INSTANCE = 1,
  MOCKA_MATCH_STARTUP_WM_CLASS_CLASS    = 2,
  MOCKA_MATCH_ID                        = 3,
  MOCKA_MATCH_PROGRAM                   = 4,
  MOCKA_MATCH_STARTUP_ID                = 5,
  MOCKA_MATCH_NONE                      = 6,
} MockaMatchStep;

MockaAppEntry *mocka_matcher_match (MockaAppIndex  *index,
                                    const gchar    *instance,
                                    const gchar    *res_class,
                                    const gchar    *startup_id,
                                    GHashTable     *launches,
                                    MockaMatchStep *step);

G_END_DECLS