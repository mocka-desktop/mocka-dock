/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

gchar *mocka_desktop_import (const gchar         *path,
                             const gchar * const *app_dirs,
                             const gchar         *user_dir,
                             GError             **error);

G_END_DECLS