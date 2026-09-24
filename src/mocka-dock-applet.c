/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <mate-panel-applet.h>

#define MOCKA_DOCK_FACTORY_ID "MockaDockAppletFactory"
#define MOCKA_DOCK_APPLET_ID  "MockaDockApplet"

#define MOCKA_TYPE_DOCK_APPLET (mocka_dock_applet_get_type ())
G_DECLARE_FINAL_TYPE (MockaDockApplet, mocka_dock_applet, MOCKA, DOCK_APPLET,
                      MatePanelApplet)

struct _MockaDockApplet
{
  MatePanelApplet parent_instance;

  GtkWidget *box;
};

G_DEFINE_TYPE (MockaDockApplet, mocka_dock_applet, PANEL_TYPE_APPLET)

static GtkOrientation
orientation_for_orient (MatePanelAppletOrient orient)
{
  switch (orient)
    {
    case MATE_PANEL_APPLET_ORIENT_LEFT:
    case MATE_PANEL_APPLET_ORIENT_RIGHT:
      return GTK_ORIENTATION_VERTICAL;
    case MATE_PANEL_APPLET_ORIENT_UP:
    case MATE_PANEL_APPLET_ORIENT_DOWN:
    default:
      return GTK_ORIENTATION_HORIZONTAL;
    }
}

static void
mocka_dock_applet_change_orient (MatePanelApplet       *applet,
                                 MatePanelAppletOrient  orient)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (applet);

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->box),
                                  orientation_for_orient (orient));
}

static void
mocka_dock_applet_class_init (MockaDockAppletClass *klass)
{
  MatePanelAppletClass *applet_class = MATE_PANEL_APPLET_CLASS (klass);

  applet_class->change_orient = mocka_dock_applet_change_orient;
}

static void
mocka_dock_applet_init (MockaDockApplet *self)
{
  GtkWidget *button;

  self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add (GTK_CONTAINER (self), self->box);

  /* Placeholder until the dock model exists (M1). */
  button = gtk_button_new_from_icon_name ("user-desktop", GTK_ICON_SIZE_BUTTON);
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text (button, _("Mocka Dock"));
  gtk_box_pack_start (GTK_BOX (self->box), button, FALSE, FALSE, 0);
}

static void
mocka_dock_applet_setup (MockaDockApplet *self)
{
  MatePanelApplet *applet = MATE_PANEL_APPLET (self);

  mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MINOR);
  mate_panel_applet_set_background_widget (applet, GTK_WIDGET (self));

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->box),
      orientation_for_orient (mate_panel_applet_get_orient (applet)));

  gtk_widget_show_all (GTK_WIDGET (self));
}

static gboolean
mocka_dock_applet_factory (MatePanelApplet *applet,
                           const gchar     *iid,
                           gpointer         user_data)
{
  if (strcmp (iid, MOCKA_DOCK_APPLET_ID) != 0)
    return FALSE;

  mocka_dock_applet_setup (MOCKA_DOCK_APPLET (applet));
  return TRUE;
}

MATE_PANEL_APPLET_OUT_PROCESS_FACTORY (MOCKA_DOCK_FACTORY_ID,
                                       MOCKA_TYPE_DOCK_APPLET,
                                       "Mocka Dock",
                                       mocka_dock_applet_factory,
                                       NULL)
