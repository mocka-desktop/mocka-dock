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

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include "app-index.h"
#include "dock-button.h"
#include "dock-model.h"
#include "window-tracker.h"

#define MOCKA_DOCK_FACTORY_ID "MockaDockAppletFactory"
#define MOCKA_DOCK_APPLET_ID  "MockaDockApplet"

/* Size window icons are read at for fallback apps, before scaling. */
#define WINDOW_ICON_SIZE 96

#define MOCKA_TYPE_DOCK_APPLET (mocka_dock_applet_get_type ())
G_DECLARE_FINAL_TYPE (MockaDockApplet, mocka_dock_applet, MOCKA, DOCK_APPLET,
                      MatePanelApplet)

struct _MockaDockApplet
{
  MatePanelApplet parent_instance;

  GtkWidget *box;
  gint size;

  WnckHandle *wnck;
  MockaAppIndex *index;
  MockaDockModel *model;
  MockaWindowTracker *tracker;
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
set_button_size (GtkWidget *button,
                 gpointer   user_data)
{
  mocka_dock_button_set_size (MOCKA_DOCK_BUTTON (button),
                              GPOINTER_TO_INT (user_data));
}

/* Buttons are square, as long on each side as the panel is thick. */
static void
mocka_dock_applet_change_size (MatePanelApplet *applet,
                               guint            size)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (applet);

  self->size = size;
  gtk_container_foreach (GTK_CONTAINER (self->box), set_button_size,
                         GINT_TO_POINTER (self->size));
}

/* Keeps the buttons in the same order as the apps in the model. */
static void
on_items_changed (GListModel *list,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (user_data);
  GList *children = gtk_container_get_children (GTK_CONTAINER (self->box));
  GList *l = g_list_nth (children, position);
  guint i;

  for (i = 0; i < removed && l != NULL; i++, l = l->next)
    gtk_widget_destroy (l->data);
  g_list_free (children);

  for (i = 0; i < added; i++)
    {
      g_autoptr(MockaDockApp) app = g_list_model_get_item (list, position + i);
      GtkWidget *button = mocka_dock_button_new (app);

      mocka_dock_button_set_size (MOCKA_DOCK_BUTTON (button), self->size);
      gtk_box_pack_start (GTK_BOX (self->box), button, FALSE, FALSE, 0);
      gtk_box_reorder_child (GTK_BOX (self->box), button, position + i);
      gtk_widget_show_all (button);
    }
}

static void
load_style (void)
{
  g_autoptr(GtkCssProvider) provider = gtk_css_provider_new ();

  /* Let the square size set the button size, not the theme's padding. */
  gtk_css_provider_load_from_data (provider,
      ".mocka-dock-button { padding: 0; min-width: 0; min-height: 0; }",
      -1, NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
      GTK_STYLE_PROVIDER (provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
mocka_dock_applet_dispose (GObject *object)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (object);

  g_clear_object (&self->tracker);
  if (self->model != NULL)
    g_signal_handlers_disconnect_by_data (self->model, self);
  g_clear_object (&self->model);
  g_clear_object (&self->index);
  g_clear_object (&self->wnck);

  G_OBJECT_CLASS (mocka_dock_applet_parent_class)->dispose (object);
}

static void
mocka_dock_applet_class_init (MockaDockAppletClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MatePanelAppletClass *applet_class = MATE_PANEL_APPLET_CLASS (klass);

  object_class->dispose = mocka_dock_applet_dispose;
  applet_class->change_orient = mocka_dock_applet_change_orient;
  applet_class->change_size = mocka_dock_applet_change_size;
}

static void
mocka_dock_applet_init (MockaDockApplet *self)
{
  /* Window actions come from the user, so act as a pager (EWMH source
   * indication 2). */
  self->wnck = wnck_handle_new (WNCK_CLIENT_TYPE_PAGER);
  wnck_handle_set_default_icon_size (self->wnck, WINDOW_ICON_SIZE);

  self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add (GTK_CONTAINER (self), self->box);
}

static void
mocka_dock_applet_setup (MockaDockApplet *self)
{
  MatePanelApplet *applet = MATE_PANEL_APPLET (self);

  load_style ();

  mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MINOR);
  mate_panel_applet_set_background_widget (applet, GTK_WIDGET (self));

  self->size = mate_panel_applet_get_size (applet);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->box),
      orientation_for_orient (mate_panel_applet_get_orient (applet)));

  self->index = mocka_app_index_new_for_system ();
  self->model = mocka_dock_model_new ();
  g_signal_connect (self->model, "items-changed",
                    G_CALLBACK (on_items_changed), self);
  self->tracker = mocka_window_tracker_new (self->wnck, self->index,
                                            self->model);

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