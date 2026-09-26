/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * The "<App> unpinned" popup with an Undo button (SPEC section 10). It
 * closes itself after 5 seconds, and emits "undo" when Undo is clicked.
 *
 * It does not grab the pointer or keyboard, so the user keeps typing and
 * clicking normally while it shows; the applet closes it when the user
 * clicks elsewhere.
 */

#include "config.h"

#include "undo-popup.h"

#include <glib/gi18n-lib.h>

#define UNDO_TIMEOUT_SECONDS 5
#define GAP 4  /* pixels between the button and the popup */

struct _MockaUndoPopup
{
  GtkWindow parent_instance;

  guint timeout_id;
};

enum
{
  SIGNAL_UNDO,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (MockaUndoPopup, mocka_undo_popup, GTK_TYPE_WINDOW)

static gboolean
on_timeout (gpointer user_data)
{
  MockaUndoPopup *self = MOCKA_UNDO_POPUP (user_data);

  self->timeout_id = 0;
  gtk_widget_destroy (GTK_WIDGET (self));
  return G_SOURCE_REMOVE;
}

static void
on_undo_clicked (GtkButton *button,
                 gpointer   user_data)
{
  MockaUndoPopup *self = MOCKA_UNDO_POPUP (user_data);

  g_signal_emit (self, signals[SIGNAL_UNDO], 0);
  gtk_widget_destroy (GTK_WIDGET (self));
}

/*
 * Shows the popup next to where the button was, on the side facing away
 * from the panel, kept inside the monitor. button is in screen pixels.
 */
void
mocka_undo_popup_show_at (MockaUndoPopup     *self,
                          const GdkRectangle *button,
                          GtkPositionType     side)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GdkDisplay *display = gtk_widget_get_display (widget);
  GdkMonitor *monitor;
  GdkRectangle area;
  GtkRequisition size;
  gint x, y;

  g_return_if_fail (MOCKA_IS_UNDO_POPUP (self));

  gtk_widget_get_preferred_size (widget, NULL, &size);
  monitor = gdk_display_get_monitor_at_point (display,
                                              button->x + button->width / 2,
                                              button->y + button->height / 2);
  gdk_monitor_get_workarea (monitor, &area);

  switch (side)
    {
    case GTK_POS_BOTTOM:
      x = button->x + (button->width - size.width) / 2;
      y = button->y + button->height + GAP;
      break;
    case GTK_POS_LEFT:
      x = button->x - size.width - GAP;
      y = button->y + (button->height - size.height) / 2;
      break;
    case GTK_POS_RIGHT:
      x = button->x + button->width + GAP;
      y = button->y + (button->height - size.height) / 2;
      break;
    case GTK_POS_TOP:
    default:
      x = button->x + (button->width - size.width) / 2;
      y = button->y - size.height - GAP;
      break;
    }

  x = CLAMP (x, area.x, area.x + area.width - size.width);
  y = CLAMP (y, area.y, area.y + area.height - size.height);

  gtk_window_move (GTK_WINDOW (self), x, y);
  gtk_widget_show_all (widget);

  if (self->timeout_id == 0)
    self->timeout_id = g_timeout_add_seconds (UNDO_TIMEOUT_SECONDS,
                                              on_timeout, self);
}

static void
mocka_undo_popup_destroy (GtkWidget *widget)
{
  MockaUndoPopup *self = MOCKA_UNDO_POPUP (widget);

  g_clear_handle_id (&self->timeout_id, g_source_remove);

  GTK_WIDGET_CLASS (mocka_undo_popup_parent_class)->destroy (widget);
}

static void
mocka_undo_popup_class_init (MockaUndoPopupClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->destroy = mocka_undo_popup_destroy;

  signals[SIGNAL_UNDO] =
    g_signal_new ("undo", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
mocka_undo_popup_init (MockaUndoPopup *self)
{
  gtk_window_set_type_hint (GTK_WINDOW (self), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
  gtk_window_set_resizable (GTK_WINDOW (self), FALSE);
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               "mocka-undo-popup");
}

GtkWidget *
mocka_undo_popup_new (const gchar *app_name)
{
  MockaUndoPopup *self = g_object_new (MOCKA_TYPE_UNDO_POPUP,
                                       "type", GTK_WINDOW_POPUP, NULL);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *undo = gtk_button_new_with_mnemonic (_("_Undo"));
  g_autofree gchar *text = NULL;

  /* Translators: %s is an application name, such as "Firefox". */
  text = g_strdup_printf (_("%s unpinned"), app_name);

  gtk_container_set_border_width (GTK_CONTAINER (box), 8);
  gtk_container_add (GTK_CONTAINER (box), gtk_label_new (text));
  gtk_container_add (GTK_CONTAINER (box), undo);
  gtk_container_add (GTK_CONTAINER (self), box);

  g_signal_connect (undo, "clicked", G_CALLBACK (on_undo_clicked), self);

  return GTK_WIDGET (self);
}