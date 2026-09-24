/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* One application desktop entry, with the fields the dock reads. */
typedef struct _MockaAppEntry MockaAppEntry;

struct _MockaAppEntry
{
  gchar *id;               /* desktop entry ID, e.g. "firefox.desktop" */
  gchar *path;
  gchar *name;
  gchar *icon;
  gchar *exec;
  gchar *startup_wm_class;
  gchar *program;          /* file name of TryExec, or of Exec's first word */
  gboolean no_display;
  gboolean exec_has_args;  /* Exec has arguments besides field codes */
};

MockaAppEntry *mocka_app_entry_ref   (MockaAppEntry *entry);
void           mocka_app_entry_unref (MockaAppEntry *entry);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MockaAppEntry, mocka_app_entry_unref)

/* Keys the matcher looks entries up by (SPEC section 6). */
typedef enum
{
  MOCKA_APP_KEY_STARTUP_WM_CLASS, /* exact */
  MOCKA_APP_KEY_ID,               /* ID without ".desktop", case-insensitive */
  MOCKA_APP_KEY_PROGRAM,          /* program file name, case-insensitive */
} MockaAppKey;

#define MOCKA_TYPE_APP_INDEX (mocka_app_index_get_type ())
G_DECLARE_FINAL_TYPE (MockaAppIndex, mocka_app_index, MOCKA, APP_INDEX, GObject)

MockaAppIndex *mocka_app_index_new            (const gchar * const *dirs);
MockaAppIndex *mocka_app_index_new_for_system (void);

void           mocka_app_index_reload         (MockaAppIndex *self);

MockaAppEntry *mocka_app_index_lookup         (MockaAppIndex *self,
                                               const gchar   *id);
GPtrArray     *mocka_app_index_find           (MockaAppIndex *self,
                                               MockaAppKey    key,
                                               const gchar   *value);

G_END_DECLS