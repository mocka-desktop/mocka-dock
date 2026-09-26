/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gtk/gtk.h>

#include "dock-button.h"

G_BEGIN_DECLS

GtkWidget *mocka_app_menu_new (MockaDockButton *button);

G_END_DECLS