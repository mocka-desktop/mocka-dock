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

/* Largest thumbnail, in logical pixels: snapshots are kept at this size. */
#define MOCKA_THUMBNAIL_WIDTH 192
#define MOCKA_THUMBNAIL_HEIGHT 120

gboolean mocka_compositor_running (GdkDisplay *display);
cairo_surface_t *mocka_window_capture (WnckWindow *window, gint width, gint height, gint scale);

cairo_surface_t *mocka_window_take_snapshot (WnckWindow *window);
cairo_surface_t *mocka_window_get_snapshot (WnckWindow *window);
cairo_surface_t *mocka_surface_fit (cairo_surface_t *surface, gint width, gint height);

G_END_DECLS