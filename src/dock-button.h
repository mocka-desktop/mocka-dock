/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gtk/gtk.h>

#include "dock-model.h"

G_BEGIN_DECLS

#define MOCKA_TYPE_DOCK_BUTTON (mocka_dock_button_get_type ())
G_DECLARE_FINAL_TYPE (MockaDockButton, mocka_dock_button, MOCKA, DOCK_BUTTON,
                      GtkButton)

GtkWidget    *mocka_dock_button_new           (MockaDockApp    *app);
MockaDockApp *mocka_dock_button_get_app       (MockaDockButton *self);
void          mocka_dock_button_set_size      (MockaDockButton *self,
                                               gint             size);
void          mocka_dock_button_set_popup_side (MockaDockButton *self,
                                                GtkPositionType  side);

G_END_DECLS