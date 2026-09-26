/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MOCKA_TYPE_UNDO_POPUP (mocka_undo_popup_get_type ())
G_DECLARE_FINAL_TYPE (MockaUndoPopup, mocka_undo_popup, MOCKA, UNDO_POPUP,
                      GtkWindow)

GtkWidget *mocka_undo_popup_new     (const gchar          *app_name);
void       mocka_undo_popup_show_at (MockaUndoPopup       *self,
                                     const GdkRectangle   *button,
                                     GtkPositionType       side);

G_END_DECLS