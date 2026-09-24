/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <glib-object.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include "app-index.h"
#include "dock-model.h"

G_BEGIN_DECLS

#define MOCKA_TYPE_WINDOW_TRACKER (mocka_window_tracker_get_type ())
G_DECLARE_FINAL_TYPE (MockaWindowTracker, mocka_window_tracker, MOCKA,
                      WINDOW_TRACKER, GObject)

MockaWindowTracker *mocka_window_tracker_new (WnckHandle     *wnck,
                                              MockaAppIndex  *index,
                                              MockaDockModel *model);

G_END_DECLS