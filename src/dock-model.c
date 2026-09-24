/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * The apps shown in the dock and their windows (SPEC section 5). Apps with
 * windows appear in the order their first window appeared, and an app is
 * removed when its last window closes.
 */

#include "config.h"

#include "dock-model.h"

struct _MockaDockApp
{
  GObject parent_instance;

  gchar *key;
  MockaAppEntry *entry;
  GPtrArray *windows;
};

enum
{
  APP_SIGNAL_WINDOWS_CHANGED,
  APP_N_SIGNALS
};

static guint app_signals[APP_N_SIGNALS];

G_DEFINE_TYPE (MockaDockApp, mocka_dock_app, G_TYPE_OBJECT)

static void
mocka_dock_app_finalize (GObject *object)
{
  MockaDockApp *self = MOCKA_DOCK_APP (object);

  g_free (self->key);
  g_clear_pointer (&self->entry, mocka_app_entry_unref);
  g_ptr_array_unref (self->windows);

  G_OBJECT_CLASS (mocka_dock_app_parent_class)->finalize (object);
}

static void
mocka_dock_app_class_init (MockaDockAppClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mocka_dock_app_finalize;

  app_signals[APP_SIGNAL_WINDOWS_CHANGED] =
    g_signal_new ("windows-changed", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
mocka_dock_app_init (MockaDockApp *self)
{
  self->windows = g_ptr_array_new ();
}

static MockaDockApp *
mocka_dock_app_new (const gchar   *key,
                    MockaAppEntry *entry)
{
  MockaDockApp *self = g_object_new (MOCKA_TYPE_DOCK_APP, NULL);

  self->key = g_strdup (key);
  if (entry != NULL)
    self->entry = mocka_app_entry_ref (entry);

  return self;
}

/* The desktop entry ID, or "class:" and the class name for the fallback. */
const gchar *
mocka_dock_app_get_key (MockaDockApp *self)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_APP (self), NULL);

  return self->key;
}

/* The app's desktop entry, or NULL for the class fallback (SPEC section 6). */
MockaAppEntry *
mocka_dock_app_get_entry (MockaDockApp *self)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_APP (self), NULL);

  return self->entry;
}

/* The app's windows, oldest first. Owned by the app. */
GPtrArray *
mocka_dock_app_get_windows (MockaDockApp *self)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_APP (self), NULL);

  return self->windows;
}

struct _MockaDockModel
{
  GObject parent_instance;

  GPtrArray *apps;        /* MockaDockApp, owned, in dock order */
  GHashTable *by_key;     /* key → MockaDockApp */
  GHashTable *by_window;  /* window → MockaDockApp */
};

static void mocka_dock_model_list_model_init (GListModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MockaDockModel, mocka_dock_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL,
                                                mocka_dock_model_list_model_init))

static GType
mocka_dock_model_get_item_type (GListModel *list)
{
  return MOCKA_TYPE_DOCK_APP;
}

static guint
mocka_dock_model_get_n_items (GListModel *list)
{
  return MOCKA_DOCK_MODEL (list)->apps->len;
}

static gpointer
mocka_dock_model_get_item (GListModel *list,
                           guint       position)
{
  MockaDockModel *self = MOCKA_DOCK_MODEL (list);

  if (position >= self->apps->len)
    return NULL;

  return g_object_ref (g_ptr_array_index (self->apps, position));
}

static void
mocka_dock_model_list_model_init (GListModelInterface *iface)
{
  iface->get_item_type = mocka_dock_model_get_item_type;
  iface->get_n_items = mocka_dock_model_get_n_items;
  iface->get_item = mocka_dock_model_get_item;
}

static void
mocka_dock_model_finalize (GObject *object)
{
  MockaDockModel *self = MOCKA_DOCK_MODEL (object);

  g_hash_table_unref (self->by_window);
  g_hash_table_unref (self->by_key);
  g_ptr_array_unref (self->apps);

  G_OBJECT_CLASS (mocka_dock_model_parent_class)->finalize (object);
}

static void
mocka_dock_model_class_init (MockaDockModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mocka_dock_model_finalize;
}

static void
mocka_dock_model_init (MockaDockModel *self)
{
  self->apps = g_ptr_array_new_with_free_func (g_object_unref);
  self->by_key = g_hash_table_new (g_str_hash, g_str_equal);
  self->by_window = g_hash_table_new (NULL, NULL);
}

MockaDockModel *
mocka_dock_model_new (void)
{
  return g_object_new (MOCKA_TYPE_DOCK_MODEL, NULL);
}

MockaDockApp *
mocka_dock_model_lookup (MockaDockModel *self,
                         const gchar    *key)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_MODEL (self), NULL);

  return g_hash_table_lookup (self->by_key, key);
}

MockaDockApp *
mocka_dock_model_get_window_app (MockaDockModel *self,
                                 gpointer        window)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_MODEL (self), NULL);

  return g_hash_table_lookup (self->by_window, window);
}

/*
 * Removes a window from its app. An app left without windows is removed
 * from the dock.
 */
void
mocka_dock_model_remove_window (MockaDockModel *self,
                                gpointer        window)
{
  MockaDockApp *app;
  guint position;

  g_return_if_fail (MOCKA_IS_DOCK_MODEL (self));

  app = g_hash_table_lookup (self->by_window, window);
  if (app == NULL)
    return;

  g_hash_table_remove (self->by_window, window);
  g_ptr_array_remove (app->windows, window);
  g_signal_emit (app, app_signals[APP_SIGNAL_WINDOWS_CHANGED], 0);

  if (app->windows->len > 0)
    return;

  g_assert (g_ptr_array_find (self->apps, app, &position));
  g_hash_table_remove (self->by_key, app->key);
  g_ptr_array_remove_index (self->apps, position);
  g_list_model_items_changed (G_LIST_MODEL (self), position, 1, 0);
}

/*
 * Adds a window to the app with this key, creating the app at the end of
 * the dock when it has no button yet. A window already in the model under
 * another key moves to the new app, which is how a class change is handled.
 */
void
mocka_dock_model_add_window (MockaDockModel *self,
                             gpointer        window,
                             const gchar    *key,
                             MockaAppEntry  *entry)
{
  MockaDockApp *app;

  g_return_if_fail (MOCKA_IS_DOCK_MODEL (self));
  g_return_if_fail (window != NULL);
  g_return_if_fail (key != NULL);

  app = g_hash_table_lookup (self->by_window, window);
  if (app != NULL)
    {
      if (g_str_equal (app->key, key))
        return;
      mocka_dock_model_remove_window (self, window);
    }

  app = g_hash_table_lookup (self->by_key, key);
  if (app == NULL)
    {
      app = mocka_dock_app_new (key, entry);
      g_ptr_array_add (self->apps, app);
      g_hash_table_insert (self->by_key, app->key, app);
      g_list_model_items_changed (G_LIST_MODEL (self), self->apps->len - 1,
                                  0, 1);
    }

  g_ptr_array_add (app->windows, window);
  g_hash_table_insert (self->by_window, window, app);
  g_signal_emit (app, app_signals[APP_SIGNAL_WINDOWS_CHANGED], 0);
}

/* The key of the app a window belongs to (see mocka_dock_app_get_key). */
gchar *
mocka_dock_app_key_for (MockaAppEntry *entry,
                        const gchar   *res_class)
{
  if (entry != NULL)
    return g_strdup (entry->id);

  return g_strconcat ("class:", res_class != NULL ? res_class : "", NULL);
}