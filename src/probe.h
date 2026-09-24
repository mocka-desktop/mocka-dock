/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * M0 risk checks (PLAN.md). Temporary: removed when the dock model and
 * buttons replace the placeholder button in M1.
 */

#pragma once

#include <gtk/gtk.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

G_BEGIN_DECLS

void probe_attach (GtkWidget  *button,
                   WnckHandle *wnck);

G_END_DECLS
