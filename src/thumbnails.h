/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gtk/gtk.h>

#include "dock-button.h"

G_BEGIN_DECLS

#define MOCKA_TYPE_THUMBNAILS (mocka_thumbnails_get_type ())
G_DECLARE_FINAL_TYPE (MockaThumbnails, mocka_thumbnails, MOCKA, THUMBNAILS, GObject)

MockaThumbnails *mocka_thumbnails_new (GtkWidget *dock);

void mocka_thumbnails_set_side (MockaThumbnails *self, GtkPositionType side);
void mocka_thumbnails_enter (MockaThumbnails *self, MockaDockButton *button);
void mocka_thumbnails_leave (MockaThumbnails *self);
void mocka_thumbnails_cancel (MockaThumbnails *self);
void mocka_thumbnails_toggle (MockaThumbnails *self, MockaDockButton *button);
void mocka_thumbnails_hide (MockaThumbnails *self);

G_END_DECLS