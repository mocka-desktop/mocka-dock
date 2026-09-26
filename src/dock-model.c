/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * The apps shown in the dock and their windows (SPEC section 5).
 *
 * Pinned apps come first, in the order the user arranged them, whether they
 * run or not. Then come apps that are not pinned and have a shown window, in
 * the order their first window appeared.
 *
 * The model keeps every window it is given, each either shown or hidden
 * (on another workspace). An app that is not pinned is forgotten when its
 * last window closes.
 */

#include "config.h"

#include "dock-model.h"

struct _MockaDockApp
{
  GObject parent_instance;

  gchar *key;
  MockaAppEntry *entry;
  gboolean pinned;
  GPtrArray *all_windows;  /* shown and hidden, oldest first */
  GPtrArray *windows;      /* shown only, oldest first */
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
  g_ptr_array_unref (self->all_windows);
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
  self->all_windows = g_ptr_array_new ();
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

gboolean
mocka_dock_app_get_pinned (MockaDockApp *self)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_APP (self), FALSE);

  return self->pinned;
}

/*
 * All the app's windows, shown or on other workspaces, oldest first. Owned
 * by the app.
 */
GPtrArray *
mocka_dock_app_get_all_windows (MockaDockApp *self)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_APP (self), NULL);

  return self->all_windows;
}

/*
 * The app's shown windows, oldest first. Counts, clicks, and lists use only
 * these (SPEC section 5). Owned by the app.
 */
GPtrArray *
mocka_dock_app_get_windows (MockaDockApp *self)
{
  g_return_val_if_fail (MOCKA_IS_DOCK_APP (self), NULL);

  return self->windows;
}

struct _MockaDockModel
{
  GObject parent_instance;

  GPtrArray *apps;        /* every known app, owned, in start order */
  GPtrArray *pinned;      /* pinned apps, in the user's order */
  GPtrArray *listed;      /* the dock: pinned apps, then running ones */
  GHashTable *by_key;     /* key → MockaDockApp */
  GHashTable *by_window;  /* window → MockaDockApp */
  GHashTable *hidden;     /* set of hidden windows */
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
  return MOCKA_DOCK_MODEL (list)->listed->len;
}

static gpointer
mocka_dock_model_get_item (GListModel *list,
                           guint       position)
{
  MockaDockModel *self = MOCKA_DOCK_MODEL (list);

  if (position >= self->listed->len)
    return NULL;

  return g_object_ref (g_ptr_array_index (self->listed, position));
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

  g_hash_table_unref (self->hidden);
  g_hash_table_unref (self->by_window);
  g_hash_table_unref (self->by_key);
  g_ptr_array_unref (self->listed);
  g_ptr_array_unref (self->pinned);
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
  self->pinned = g_ptr_array_new ();
  self->listed = g_ptr_array_new ();
  self->by_key = g_hash_table_new (g_str_hash, g_str_equal);
  self->by_window = g_hash_table_new (NULL, NULL);
  self->hidden = g_hash_table_new (NULL, NULL);
}

MockaDockModel *
mocka_dock_model_new (void)
{
  return g_object_new (MOCKA_TYPE_DOCK_MODEL, NULL);
}

/* Rebuilds the app's shown windows, and says so when they changed. */
static void
refresh_windows (MockaDockModel *self,
                 MockaDockApp   *app)
{
  g_autoptr(GPtrArray) shown = g_ptr_array_new ();
  gboolean changed;
  guint i;

  for (i = 0; i < app->all_windows->len; i++)
    {
      gpointer window = g_ptr_array_index (app->all_windows, i);

      if (!g_hash_table_contains (self->hidden, window))
        g_ptr_array_add (shown, window);
    }

  changed = shown->len != app->windows->len;
  for (i = 0; !changed && i < shown->len; i++)
    changed = g_ptr_array_index (shown, i) != g_ptr_array_index (app->windows, i);

  if (!changed)
    return;

  g_ptr_array_set_size (app->windows, 0);
  for (i = 0; i < shown->len; i++)
    g_ptr_array_add (app->windows, g_ptr_array_index (shown, i));

  g_signal_emit (app, app_signals[APP_SIGNAL_WINDOWS_CHANGED], 0);
}

/*
 * Rebuilds the dock (pinned apps, then apps with a shown window) and reports
 * only the part that changed, so buttons outside it are kept. Then forgets
 * apps that are neither pinned nor have windows.
 */
static void
sync_listed (MockaDockModel *self)
{
  g_autoptr(GPtrArray) listed = g_ptr_array_new ();
  guint prefix = 0, suffix = 0;
  guint old_len = self->listed->len;
  guint i;

  g_ptr_array_extend (listed, self->pinned, NULL, NULL);
  for (i = 0; i < self->apps->len; i++)
    {
      MockaDockApp *app = g_ptr_array_index (self->apps, i);

      if (!app->pinned && app->windows->len > 0)
        g_ptr_array_add (listed, app);
    }

  while (prefix < old_len && prefix < listed->len
         && g_ptr_array_index (self->listed, prefix)
            == g_ptr_array_index (listed, prefix))
    prefix++;

  while (suffix < old_len - prefix && suffix < listed->len - prefix
         && g_ptr_array_index (self->listed, old_len - 1 - suffix)
            == g_ptr_array_index (listed, listed->len - 1 - suffix))
    suffix++;

  if (prefix + suffix < old_len || prefix + suffix < listed->len)
    {
      g_ptr_array_set_size (self->listed, 0);
      g_ptr_array_extend (self->listed, listed, NULL, NULL);
      g_list_model_items_changed (G_LIST_MODEL (self), prefix,
                                  old_len - prefix - suffix,
                                  listed->len - prefix - suffix);
    }

  for (i = self->apps->len; i > 0; i--)
    {
      MockaDockApp *app = g_ptr_array_index (self->apps, i - 1);

      if (!app->pinned && app->all_windows->len == 0)
        {
          g_hash_table_remove (self->by_key, app->key);
          g_ptr_array_remove_index (self->apps, i - 1);
        }
    }
}

/* The app with this key, creating it after all known apps if needed. */
static MockaDockApp *
ensure_app (MockaDockModel *self,
            const gchar    *key,
            MockaAppEntry  *entry)
{
  MockaDockApp *app = g_hash_table_lookup (self->by_key, key);

  if (app == NULL)
    {
      app = mocka_dock_app_new (key, entry);
      g_ptr_array_add (self->apps, app);
      g_hash_table_insert (self->by_key, app->key, app);
    }
  else if (app->entry == NULL && entry != NULL)
    {
      app->entry = mocka_app_entry_ref (entry);
    }

  return app;
}

/*
 * Sets the pinned apps, in order (SPEC section 10). Apps no longer pinned
 * keep their button while they have shown windows, after the pinned ones.
 */
void
mocka_dock_model_set_pinned (MockaDockModel *self,
                             GPtrArray      *entries)
{
  guint i;

  g_return_if_fail (MOCKA_IS_DOCK_MODEL (self));
  g_return_if_fail (entries != NULL);

  for (i = 0; i < self->pinned->len; i++)
    ((MockaDockApp *) g_ptr_array_index (self->pinned, i))->pinned = FALSE;
  g_ptr_array_set_size (self->pinned, 0);

  for (i = 0; i < entries->len; i++)
    {
      MockaAppEntry *entry = g_ptr_array_index (entries, i);
      MockaDockApp *app = ensure_app (self, entry->id, entry);

      if (app->pinned)
        continue;
      app->pinned = TRUE;
      g_ptr_array_add (self->pinned, app);
    }

  sync_listed (self);
}

/* The app with this key, listed or not, or NULL. */
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
 * Removes a window from its app. An app left without shown windows leaves
 * the dock, and is forgotten once it has no windows at all.
 */
void
mocka_dock_model_remove_window (MockaDockModel *self,
                                gpointer        window)
{
  MockaDockApp *app;

  g_return_if_fail (MOCKA_IS_DOCK_MODEL (self));

  app = g_hash_table_lookup (self->by_window, window);
  if (app == NULL)
    return;

  g_hash_table_remove (self->by_window, window);
  g_hash_table_remove (self->hidden, window);
  g_ptr_array_remove (app->all_windows, window);

  refresh_windows (self, app);
  sync_listed (self);
}

/*
 * Shows or hides a window, for example when it is on another workspace
 * (SPEC section 5).
 */
void
mocka_dock_model_set_window_visible (MockaDockModel *self,
                                     gpointer        window,
                                     gboolean        visible)
{
  MockaDockApp *app;

  g_return_if_fail (MOCKA_IS_DOCK_MODEL (self));

  app = g_hash_table_lookup (self->by_window, window);
  if (app == NULL || visible != g_hash_table_contains (self->hidden, window))
    return;

  if (visible)
    g_hash_table_remove (self->hidden, window);
  else
    g_hash_table_add (self->hidden, window);

  refresh_windows (self, app);
  sync_listed (self);
}

/*
 * Adds a window to the app with this key. An app seen for the first time
 * goes after all others. A window already in the model under another key
 * moves to the new app, which is how a class change is handled.
 */
void
mocka_dock_model_add_window (MockaDockModel *self,
                             gpointer        window,
                             const gchar    *key,
                             MockaAppEntry  *entry,
                             gboolean        visible)
{
  MockaDockApp *app;

  g_return_if_fail (MOCKA_IS_DOCK_MODEL (self));
  g_return_if_fail (window != NULL);
  g_return_if_fail (key != NULL);

  app = g_hash_table_lookup (self->by_window, window);
  if (app != NULL)
    {
      if (g_str_equal (app->key, key))
        {
          mocka_dock_model_set_window_visible (self, window, visible);
          return;
        }
      mocka_dock_model_remove_window (self, window);
    }

  app = ensure_app (self, key, entry);

  /* Start order counts from an app's first window, also for pinned apps. */
  if (app->all_windows->len == 0)
    {
      g_ptr_array_add (self->apps, g_object_ref (app));
      g_ptr_array_remove (self->apps, app);
    }

  g_ptr_array_add (app->all_windows, window);
  g_hash_table_insert (self->by_window, window, app);
  if (!visible)
    g_hash_table_add (self->hidden, window);

  refresh_windows (self, app);
  sync_listed (self);
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