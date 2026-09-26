/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * App menu (SPEC section 9.1). Launches go through the button's "launch"
 * signal and pinning through its "pin" and "unpin" signals, so the applet
 * handles them like clicks.
 */

#include "config.h"

#include "app-menu.h"

#include <glib/gi18n-lib.h>
#include <gio/gdesktopappinfo.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include "recent-match.h"

/* The most recent files listed; older ones are left out. */
#define MAX_RECENT_FILES 5

static void
on_action_activate (GtkMenuItem *item,
                    gpointer     user_data)
{
  g_signal_emit_by_name (user_data, "launch",
                         g_object_get_data (G_OBJECT (item), "action"), NULL);
}

static void
on_recent_activate (GtkMenuItem *item,
                    gpointer     user_data)
{
  const gchar *uris[] = { g_object_get_data (G_OBJECT (item), "uri"), NULL };

  g_signal_emit_by_name (user_data, "launch", NULL, uris);
}

static void
on_new_instance_activate (GtkMenuItem *item,
                          gpointer     user_data)
{
  g_signal_emit_by_name (user_data, "launch", NULL, NULL);
}

static void
on_pin_activate (GtkMenuItem *item,
                 gpointer     user_data)
{
  MockaDockApp *app = mocka_dock_button_get_app (MOCKA_DOCK_BUTTON (user_data));

  g_signal_emit_by_name (user_data,
                         mocka_dock_app_get_pinned (app) ? "unpin" : "pin");
}

/* Closes the windows the dock shows for the app (SPEC section 5). */
static void
on_close_activate (GtkMenuItem *item,
                   gpointer     user_data)
{
  MockaDockApp *app = mocka_dock_button_get_app (MOCKA_DOCK_BUTTON (user_data));
  GPtrArray *windows = mocka_dock_app_get_windows (app);
  guint32 time = gtk_get_current_event_time ();
  guint i;

  /* Only asks the window manager, so the windows stay listed meanwhile. */
  for (i = 0; i < windows->len; i++)
    wnck_window_close (g_ptr_array_index (windows, i), time);
}

static GtkWidget *
append_item (GtkWidget   *menu,
             const gchar *label,
             GCallback    callback,
             gpointer     button)
{
  GtkWidget *item = gtk_menu_item_new_with_label (label);

  g_signal_connect_object (item, "activate", callback, button, 0);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  return item;
}

/* A separator between groups, only once there is something above it. */
static void
append_separator (GtkWidget *menu)
{
  g_autoptr(GList) children = gtk_container_get_children (GTK_CONTAINER (menu));

  if (children != NULL)
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());
}

/* All the actions of the app's desktop entry, in the entry's order. */
static void
append_actions (GtkWidget       *menu,
                MockaDockButton *button,
                MockaAppEntry   *entry)
{
  g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new_from_filename (entry->path);
  const gchar * const *actions;
  guint i;

  if (info == NULL)
    return;

  actions = g_desktop_app_info_list_actions (info);
  for (i = 0; actions[i] != NULL; i++)
    {
      g_autofree gchar *name = g_desktop_app_info_get_action_name (info, actions[i]);
      GtkWidget *item = append_item (menu, name, G_CALLBACK (on_action_activate),
                                     button);

      g_object_set_data_full (G_OBJECT (item), "action", g_strdup (actions[i]),
                              g_free);
    }
}

/* Whether one of the applications recorded on a recent file is this app. */
static gboolean
recent_info_is_for (GtkRecentInfo *info,
                    MockaAppEntry *entry)
{
  g_auto(GStrv) apps = gtk_recent_info_get_applications (info, NULL);
  guint i;

  for (i = 0; apps != NULL && apps[i] != NULL; i++)
    {
      const gchar *exec = NULL;

      gtk_recent_info_get_application_info (info, apps[i], &exec, NULL, NULL);
      if (mocka_recent_app_matches (entry, apps[i], exec))
        return TRUE;
    }

  return FALSE;
}

static gint
compare_recent (gconstpointer a,
                gconstpointer b)
{
  time_t ma = gtk_recent_info_get_modified ((GtkRecentInfo *) a);
  time_t mb = gtk_recent_info_get_modified ((GtkRecentInfo *) b);

  return ma < mb ? 1 : ma > mb ? -1 : 0;
}

static GtkWidget *
recent_item_new (GtkRecentInfo *info)
{
  GtkWidget *item = gtk_menu_item_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *label = gtk_label_new (gtk_recent_info_get_display_name (info));
  g_autoptr(GIcon) icon = gtk_recent_info_get_gicon (info);
  g_autofree gchar *location = gtk_recent_info_get_uri_display (info);

  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 40);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  if (icon != NULL)
    gtk_container_add (GTK_CONTAINER (box),
                       gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU));
  gtk_container_add (GTK_CONTAINER (box), label);
  gtk_container_add (GTK_CONTAINER (item), box);
  gtk_widget_set_tooltip_text (item, location);

  g_object_set_data_full (G_OBJECT (item), "uri",
                          g_strdup (gtk_recent_info_get_uri (info)), g_free);

  return item;
}

/* The app's most recent files that still exist, newest first. */
static void
append_recent_files (GtkWidget       *menu,
                     MockaDockButton *button,
                     MockaAppEntry   *entry)
{
  GList *items = gtk_recent_manager_get_items (gtk_recent_manager_get_default ());
  GList *mine = NULL;
  GList *l;
  guint n;

  for (l = items; l != NULL; l = l->next)
    if (gtk_recent_info_exists (l->data) && recent_info_is_for (l->data, entry))
      mine = g_list_prepend (mine, l->data);

  if (mine != NULL)
    append_separator (menu);

  mine = g_list_sort (mine, compare_recent);
  for (l = mine, n = 0; l != NULL && n < MAX_RECENT_FILES; l = l->next, n++)
    {
      GtkWidget *item = recent_item_new (l->data);

      g_signal_connect_object (item, "activate", G_CALLBACK (on_recent_activate),
                               button, 0);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

  g_list_free (mine);
  g_list_free_full (items, (GDestroyNotify) gtk_recent_info_unref);
}

/*
 * From top to bottom: desktop actions, recent files, the app's name for a
 * new instance, Pin to dock or Unpin from dock, and closing its windows.
 * Apps without a desktop entry only have the last. Returns NULL when the
 * menu would be empty.
 */
GtkWidget *
mocka_app_menu_new (MockaDockButton *button)
{
  MockaDockApp *app = mocka_dock_button_get_app (button);
  MockaAppEntry *entry = mocka_dock_app_get_entry (app);
  guint n_windows = mocka_dock_app_get_windows (app)->len;
  GtkWidget *menu;

  if (entry == NULL && n_windows == 0)
    return NULL;

  menu = gtk_menu_new ();

  if (entry != NULL)
    {
      append_actions (menu, button, entry);
      append_recent_files (menu, button, entry);

      append_separator (menu);
      append_item (menu, entry->name != NULL ? entry->name : entry->id,
                   G_CALLBACK (on_new_instance_activate), button);
      append_item (menu, mocka_dock_app_get_pinned (app)
                         ? _("Unpin from dock") : _("Pin to dock"),
                   G_CALLBACK (on_pin_activate), button);
    }

  if (n_windows > 0)
    {
      append_separator (menu);
      append_item (menu, n_windows > 1 ? _("Close all windows") : _("Close window"),
                   G_CALLBACK (on_close_activate), button);
    }

  gtk_widget_show_all (menu);
  return menu;
}