/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

gchar *mocka_startup_message_get_value (const gchar *message,
                                        const gchar *type,
                                        const gchar *key);

G_END_DECLS
