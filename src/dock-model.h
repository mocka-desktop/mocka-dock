/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gio/gio.h>

#include "app-index.h"

G_BEGIN_DECLS

/*
 * One dock button: an app and its windows. Windows are opaque handles owned
 * by the caller (WnckWindow in the applet).
 */
#define MOCKA_TYPE_DOCK_APP (mocka_dock_app_get_type ())
G_DECLARE_FINAL_TYPE (MockaDockApp, mocka_dock_app, MOCKA, DOCK_APP, GObject)

const gchar   *mocka_dock_app_get_key     (MockaDockApp *self);
MockaAppEntry *mocka_dock_app_get_entry   (MockaDockApp *self);
gboolean       mocka_dock_app_get_pinned  (MockaDockApp *self);
GPtrArray     *mocka_dock_app_get_windows (MockaDockApp *self);
GPtrArray     *mocka_dock_app_get_all_windows (MockaDockApp *self);

/* The ordered list of apps shown in the dock, as a GListModel. */
#define MOCKA_TYPE_DOCK_MODEL (mocka_dock_model_get_type ())
G_DECLARE_FINAL_TYPE (MockaDockModel, mocka_dock_model, MOCKA, DOCK_MODEL, GObject)

MockaDockModel *mocka_dock_model_new           (void);

void            mocka_dock_model_add_window    (MockaDockModel *self,
                                                gpointer        window,
                                                const gchar    *key,
                                                MockaAppEntry  *entry,
                                                gboolean        visible);
void            mocka_dock_model_remove_window (MockaDockModel *self,
                                                gpointer        window);
void            mocka_dock_model_set_pinned    (MockaDockModel *self,
                                                GPtrArray      *entries);
void            mocka_dock_model_set_window_visible (MockaDockModel *self,
                                                     gpointer        window,
                                                     gboolean        visible);

MockaDockApp   *mocka_dock_model_lookup        (MockaDockModel *self,
                                                const gchar    *key);
MockaDockApp   *mocka_dock_model_get_window_app (MockaDockModel *self,
                                                 gpointer        window);

gchar          *mocka_dock_app_key_for         (MockaAppEntry  *entry,
                                                const gchar    *res_class);

G_END_DECLS