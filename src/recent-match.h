/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <glib.h>

#include "app-index.h"

G_BEGIN_DECLS

gboolean mocka_recent_app_matches (MockaAppEntry *entry,
                                   const gchar   *app_name,
                                   const gchar   *app_exec);

G_END_DECLS