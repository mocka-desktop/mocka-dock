/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gtk/gtk.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

G_BEGIN_DECLS

gboolean mocka_compositor_running (GdkDisplay *display);
cairo_surface_t *mocka_window_capture (WnckWindow *window, gint width, gint height, gint scale);

G_END_DECLS