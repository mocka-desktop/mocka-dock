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
#include <mate-panel-applet-gsettings.h>

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
  GtkPositionType popup_side;

  GSettings *settings;         /* per dock (SPEC section 16) */
  GSettings *shared_settings;  /* shared by all docks: pinned-apps */
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

/* The orient says which way the applet faces: UP on a bottom panel. */
static GtkPositionType
popup_side_for_orient (MatePanelAppletOrient orient)
{
  switch (orient)
    {
    case MATE_PANEL_APPLET_ORIENT_DOWN:
      return GTK_POS_BOTTOM;
    case MATE_PANEL_APPLET_ORIENT_LEFT:
      return GTK_POS_LEFT;
    case MATE_PANEL_APPLET_ORIENT_RIGHT:
      return GTK_POS_RIGHT;
    case MATE_PANEL_APPLET_ORIENT_UP:
    default:
      return GTK_POS_TOP;
    }
}

static void
set_button_popup_side (GtkWidget *button,
                       gpointer   user_data)
{
  mocka_dock_button_set_popup_side (MOCKA_DOCK_BUTTON (button),
                                    GPOINTER_TO_INT (user_data));
}

static void
apply_orient (MockaDockApplet       *self,
              MatePanelAppletOrient  orient)
{
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self->box),
                                  orientation_for_orient (orient));
  self->popup_side = popup_side_for_orient (orient);
  gtk_container_foreach (GTK_CONTAINER (self->box), set_button_popup_side,
                         GINT_TO_POINTER (self->popup_side));
}

static void
mocka_dock_applet_change_orient (MatePanelApplet       *applet,
                                 MatePanelAppletOrient  orient)
{
  apply_orient (MOCKA_DOCK_APPLET (applet), orient);
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
      mocka_dock_button_set_popup_side (MOCKA_DOCK_BUTTON (button),
                                        self->popup_side);
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

/*
 * Pinned apps from the shared pinned-apps setting (SPEC section 10). IDs of
 * apps that are not installed are kept in the setting but not shown.
 */
static void
update_pinned (MockaDockApplet *self)
{
  g_auto(GStrv) ids = g_settings_get_strv (self->shared_settings, "pinned-apps");
  g_autoptr(GPtrArray) entries = g_ptr_array_new ();
  guint i;

  for (i = 0; ids[i] != NULL; i++)
    {
      MockaAppEntry *entry = mocka_app_index_lookup (self->index, ids[i]);

      if (entry != NULL)
        g_ptr_array_add (entries, entry);
    }

  mocka_dock_model_set_pinned (self->model, entries);
}

static void
mocka_dock_applet_dispose (GObject *object)
{
  MockaDockApplet *self = MOCKA_DOCK_APPLET (object);

  g_clear_object (&self->settings);
  g_clear_object (&self->shared_settings);
  g_clear_object (&self->tracker);
  if (self->model != NULL)
    g_signal_handlers_disconnect_by_data (self->model, self);
  g_clear_object (&self->model);
  if (self->index != NULL)
    g_signal_handlers_disconnect_by_data (self->index, self);
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
  apply_orient (self, mate_panel_applet_get_orient (applet));

  self->index = mocka_app_index_new_for_system ();
  self->model = mocka_dock_model_new ();
  g_signal_connect (self->model, "items-changed",
                    G_CALLBACK (on_items_changed), self);
  self->tracker = mocka_window_tracker_new (self->wnck, self->index,
                                            self->model);

  self->settings = mate_panel_applet_settings_new (applet,
      (gchar *) "org.mocka_desktop.Dock.Instance");
  g_settings_bind (self->settings, "show-all-workspaces",
                   self->tracker, "show-all-workspaces", G_SETTINGS_BIND_GET);

  self->shared_settings = g_settings_new ("org.mocka_desktop.Dock");
  g_signal_connect_swapped (self->shared_settings, "changed::pinned-apps",
                            G_CALLBACK (update_pinned), self);
  g_signal_connect_swapped (self->index, "changed",
                            G_CALLBACK (update_pinned), self);
  update_pinned (self);

  /* The focused app's button is highlighted (SPEC section 12). */
  g_signal_connect_object (wnck_handle_get_default_screen (self->wnck),
                           "active-window-changed",
                           G_CALLBACK (gtk_widget_queue_draw), self->box,
                           G_CONNECT_SWAPPED);

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