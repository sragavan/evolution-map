#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include "camel-map-store-summary.h"

#define S_LOCK(x) (g_static_rec_mutex_lock(&(x)->priv->s_lock))
#define S_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->priv->s_lock))

#define STORE_GROUP_NAME "##storepriv"
#define CURRENT_SUMMARY_VERSION 1

struct _CamelMapStoreSummaryPrivate {
	GKeyFile *key_file;
	gboolean dirty;
	gchar *path;
	/* Note: We use the *same* strings in both of these hash tables, and
	 * only id_fname_hash has g_free() hooked up as the destructor func.
	 * So entries must always be removed from fname_id_hash *first*. */
	GHashTable *id_fname_hash;
	GHashTable *fname_id_hash;
	GStaticRecMutex s_lock;

	GFileMonitor *monitor_delete;
};

G_DEFINE_TYPE (CamelMapStoreSummary, camel_map_store_summary, CAMEL_TYPE_OBJECT)

static void
map_store_summary_finalize (GObject *object)
{
	CamelMapStoreSummary *map_summary = CAMEL_MAP_STORE_SUMMARY (object);
	CamelMapStoreSummaryPrivate *priv = map_summary->priv;

	g_key_file_free (priv->key_file);
	g_free (priv->path);
	g_hash_table_destroy (priv->fname_id_hash);
	g_hash_table_destroy (priv->id_fname_hash);
	g_static_rec_mutex_free (&priv->s_lock);
	if (priv->monitor_delete)
		g_object_unref (priv->monitor_delete);

	g_free (priv);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_map_store_summary_parent_class)->finalize (object);
}

static void
camel_map_store_summary_class_init (CamelMapStoreSummaryClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = map_store_summary_finalize;
}

static void
camel_map_store_summary_init (CamelMapStoreSummary *map_summary)
{
	CamelMapStoreSummaryPrivate *priv;

	priv = g_new0 (CamelMapStoreSummaryPrivate, 1);
	map_summary->priv = priv;

	priv->key_file = g_key_file_new ();
	priv->dirty = FALSE;
	priv->fname_id_hash = g_hash_table_new (g_str_hash, g_str_equal);
	priv->id_fname_hash = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	g_static_rec_mutex_init (&priv->s_lock);
}

static gchar *build_full_name (CamelMapStoreSummary *map_summary, const gchar *fid)
{
	gchar *pfid, *dname, *ret;
	gchar *pname = NULL;

	dname = camel_map_store_summary_get_folder_name (map_summary, fid, NULL);
	if (!dname)
		return NULL;

	pfid = camel_map_store_summary_get_parent_folder_id (map_summary, fid, NULL);
	if (pfid) {
		pname = build_full_name (map_summary, pfid);
		g_free (pfid);
	}

	if (pname) {
		ret = g_strdup_printf ("%s/%s", pname, dname);
		g_free (pname);
		g_free (dname);
	} else
		ret = dname;

	return ret;
}

static void
load_id_fname_hash (CamelMapStoreSummary *map_summary)
{
	GSList *folders, *l;

	g_hash_table_remove_all (map_summary->priv->fname_id_hash);
	g_hash_table_remove_all (map_summary->priv->id_fname_hash);

	folders = camel_map_store_summary_get_folders (map_summary, NULL);

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		gchar *id = l->data;
		gchar *fname;

		fname = build_full_name (map_summary, id);

		if (!fname) {
			/* eep */
			g_warning ("Cannot build full name for folder %s", id);
			g_free (id);
			continue;
		}
		g_hash_table_insert (map_summary->priv->fname_id_hash, fname, id);
		g_hash_table_insert (map_summary->priv->id_fname_hash, id, fname);
	}

	g_slist_free (folders);
}

/* we only care about delete and ignore create */
static void
monitor_delete_cb (GFileMonitor *monitor,
                   GFile *file,
                   GFile *other_file,
                   GFileMonitorEvent event,
                   gpointer user_data)
{
	CamelMapStoreSummary *map_summary = (CamelMapStoreSummary *) user_data;

	if (event == G_FILE_MONITOR_EVENT_DELETED) {
		S_LOCK (map_summary);

		if (map_summary->priv->key_file)
			camel_map_store_summary_clear (map_summary);

		S_UNLOCK (map_summary);
	}
}

CamelMapStoreSummary *
camel_map_store_summary_new (const gchar *path)
{
	CamelMapStoreSummary *map_summary;
	GError *error = NULL;
	GFile *file;

	map_summary = g_object_new (CAMEL_TYPE_MAP_STORE_SUMMARY, NULL);

	map_summary->priv->path = g_strdup (path);
	file = g_file_new_for_path (path);
	map_summary->priv->monitor_delete = g_file_monitor_file (
		file, G_FILE_MONITOR_SEND_MOVED, NULL, &error);

	/* Remove this once we have camel_store_remove_storage api,
	 * which should be available from 3.2 */
	if (!error) {
		g_signal_connect (
			map_summary->priv->monitor_delete, "changed",
			G_CALLBACK (monitor_delete_cb), map_summary);
	} else {
		g_warning (
			"CamelMapStoreSummary: "
			"Error create monitor_delete: %s \n",
			error->message);
		g_clear_error (&error);
	}

	g_object_unref (file);

	return map_summary;
}

gboolean
camel_map_store_summary_load (CamelMapStoreSummary *map_summary,
                              GError **error)
{
	CamelMapStoreSummaryPrivate *priv = map_summary->priv;
	gboolean ret;
	gint version;

	S_LOCK (map_summary);

	ret = g_key_file_load_from_file (
		priv->key_file, priv->path, 0, error);

	version = g_key_file_get_integer (
		priv->key_file, STORE_GROUP_NAME, "Version", NULL);

	if (version != CURRENT_SUMMARY_VERSION) {
		/* version doesn't match, get folders again */
		camel_map_store_summary_clear (map_summary);

		g_key_file_set_integer (
			priv->key_file, STORE_GROUP_NAME,
			"Version", CURRENT_SUMMARY_VERSION);
	}

	load_id_fname_hash (map_summary);

	S_UNLOCK (map_summary);

	return ret;
}

gboolean
camel_map_store_summary_save (CamelMapStoreSummary *map_summary,
                              GError **error)
{
	CamelMapStoreSummaryPrivate *priv = map_summary->priv;
	gboolean ret = TRUE;
	GFile *file;
	gchar *contents = NULL;

	S_LOCK (map_summary);

	if (!priv->dirty)
		goto exit;

	contents = g_key_file_to_data (
		priv->key_file, NULL, NULL);
	file = g_file_new_for_path (priv->path);
	ret = g_file_replace_contents (
		file, contents, strlen (contents),
		NULL, FALSE, G_FILE_CREATE_PRIVATE,
		NULL, NULL, error);
	g_object_unref (file);
	priv->dirty = FALSE;

exit:
	S_UNLOCK (map_summary);

	g_free (contents);
	return ret;
}

gboolean
camel_map_store_summary_clear (CamelMapStoreSummary *map_summary)
{

	S_LOCK (map_summary);

	g_key_file_free (map_summary->priv->key_file);
	map_summary->priv->key_file = g_key_file_new ();
	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);

	return TRUE;
}

gboolean
camel_map_store_summary_remove (CamelMapStoreSummary *map_summary)
{
	gint ret;

	S_LOCK (map_summary);

	if (map_summary->priv->key_file)
		camel_map_store_summary_clear (map_summary);

	ret = g_unlink (map_summary->priv->path);

	S_UNLOCK (map_summary);

	return (ret == 0);
}

void
camel_map_store_summary_rebuild_hashes (CamelMapStoreSummary *map_summary)
{
	g_return_if_fail (CAMEL_IS_MAP_STORE_SUMMARY (map_summary));

	S_LOCK (map_summary);
	load_id_fname_hash (map_summary);
	S_UNLOCK (map_summary);
}

struct subfolder_match {
	GSList *ids;
	gchar *match;
	gsize matchlen;
};

static void
match_subfolder (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
	struct subfolder_match *sm = user_data;

	if (!strncmp (key, sm->match, sm->matchlen))
		sm->ids = g_slist_prepend (sm->ids, g_strdup (value));
}

/* Must be called with the summary lock held, and gets to keep
 * both its string arguments */
static void
map_ss_hash_replace (CamelMapStoreSummary *map_summary,
                     gchar *folder_id,
                     gchar *full_name,
                     gboolean recurse)
{
	const gchar *ofname;
	struct subfolder_match sm = { NULL, NULL };

	if (!full_name)
		full_name = build_full_name (map_summary, folder_id);

	ofname = g_hash_table_lookup (
		map_summary->priv->id_fname_hash, folder_id);
	/* Remove the old fullname->id hash entry *iff* it's pointing
	 * to this folder id. */
	if (ofname) {
		gchar *ofid = g_hash_table_lookup (
			map_summary->priv->fname_id_hash, ofname);
		if (!strcmp (folder_id, ofid)) {
			g_hash_table_remove (
				map_summary->priv->fname_id_hash, ofname);
			if (recurse)
				sm.match = g_strdup_printf ("%s/", ofname);
		}
	}

	g_hash_table_insert (map_summary->priv->fname_id_hash, full_name, folder_id);

	/* Replace, not insert. The difference is that it frees the *old* folder_id
	 * key, not the new one which we just inserted into fname_id_hash too. */
	g_hash_table_replace (map_summary->priv->id_fname_hash, folder_id, full_name);

	if (sm.match) {
		GSList *l;

		sm.matchlen = strlen (sm.match);

		g_hash_table_foreach (
			map_summary->priv->fname_id_hash,
			match_subfolder, &sm);

		for (l = sm.ids; l; l = g_slist_next (l))
			map_ss_hash_replace (map_summary, l->data, NULL, FALSE);

		g_slist_free (sm.ids);
		g_free (sm.match);
	}
}

void
camel_map_store_summary_set_folder_name (CamelMapStoreSummary *map_summary,
                                         const gchar *folder_id,
                                         const gchar *display_name)
{
	S_LOCK (map_summary);

	g_key_file_set_string (
		map_summary->priv->key_file,
		folder_id, "DisplayName", display_name);

	map_ss_hash_replace (map_summary, g_strdup (folder_id), NULL, TRUE);
	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}

void
camel_map_store_summary_new_folder (CamelMapStoreSummary *map_summary,
                                    const gchar *folder_id,
                                    const gchar *parent_fid,
                                    const gchar *change_key,
                                    const gchar *display_name,
                                    guint64 folder_flags,
                                    guint64 total)
{

	S_LOCK (map_summary);

	if (parent_fid)
		g_key_file_set_string (
			map_summary->priv->key_file,
			folder_id, "ParentFolderId", parent_fid);
	if (change_key)
		g_key_file_set_string (
			map_summary->priv->key_file,
			folder_id, "ChangeKey", change_key);
	g_key_file_set_string (
		map_summary->priv->key_file,
		folder_id, "DisplayName", display_name);
	g_key_file_set_uint64 (
		map_summary->priv->key_file,
		folder_id, "Flags", folder_flags);
	g_key_file_set_uint64 (
		map_summary->priv->key_file,
		folder_id, "Total", total);

	map_ss_hash_replace (map_summary, g_strdup (folder_id), NULL, FALSE);

	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}

void
camel_map_store_summary_set_parent_folder_id (CamelMapStoreSummary *map_summary,
                                              const gchar *folder_id,
                                              const gchar *parent_id)
{
	S_LOCK (map_summary);

	if (parent_id)
		g_key_file_set_string (
			map_summary->priv->key_file,
			folder_id, "ParentFolderId", parent_id);
	else
		g_key_file_remove_key (
			map_summary->priv->key_file,
			folder_id, "ParentFolderId", NULL);

	map_ss_hash_replace (map_summary, g_strdup (folder_id), NULL, TRUE);

	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}

void
camel_map_store_summary_set_change_key (CamelMapStoreSummary *map_summary,
                                         const gchar *folder_id,
                                         const gchar *change_key)
{
	S_LOCK (map_summary);

	g_key_file_set_string (
		map_summary->priv->key_file,
		folder_id, "ChangeKey", change_key);
	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}

void
camel_map_store_summary_set_sync_state (CamelMapStoreSummary *map_summary,
                                        const gchar *folder_id,
                                        const gchar *sync_state)
{
	S_LOCK (map_summary);

	g_key_file_set_string (
		map_summary->priv->key_file,
		folder_id, "SyncState", sync_state);
	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}

void
camel_map_store_summary_set_folder_flags (CamelMapStoreSummary *map_summary,
                                          const gchar *folder_id,
                                          guint64 flags)
{
	S_LOCK (map_summary);

	g_key_file_set_uint64 (
		map_summary->priv->key_file,
		folder_id, "Flags", flags);
	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}

void
camel_map_store_summary_set_folder_unread (CamelMapStoreSummary *map_summary,
                                           const gchar *folder_id,
                                           guint64 unread)
{
	S_LOCK (map_summary);

	g_key_file_set_uint64 (
		map_summary->priv->key_file,
		folder_id, "UnRead", unread);
	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}

void
camel_map_store_summary_set_folder_total (CamelMapStoreSummary *map_summary,
                                          const gchar *folder_id,
                                          guint64 total)
{
	S_LOCK (map_summary);

	g_key_file_set_uint64 (
		map_summary->priv->key_file,
		folder_id, "Total", total);
	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}



void
camel_map_store_summary_store_string_val (CamelMapStoreSummary *map_summary,
                                          const gchar *key,
                                          const gchar *value)
{
	S_LOCK (map_summary);

	g_key_file_set_string (
		map_summary->priv->key_file,
		STORE_GROUP_NAME, key, value);
	map_summary->priv->dirty = TRUE;

	S_UNLOCK (map_summary);
}

gchar *
camel_map_store_summary_get_folder_name (CamelMapStoreSummary *map_summary,
                                         const gchar *folder_id,
                                         GError **error)
{
	gchar *ret;

	S_LOCK (map_summary);

	ret = g_key_file_get_string (
		map_summary->priv->key_file, folder_id,
		"DisplayName", error);

	S_UNLOCK (map_summary);

	return ret;
}

gchar *
camel_map_store_summary_get_folder_full_name (CamelMapStoreSummary *map_summary,
                                              const gchar *folder_id,
                                              GError **error)
{
	gchar *ret;

	S_LOCK (map_summary);

	ret = g_hash_table_lookup (map_summary->priv->id_fname_hash, folder_id);

	if (ret)
		ret = g_strdup (ret);

	S_UNLOCK (map_summary);

	return ret;
}

gchar *
camel_map_store_summary_get_parent_folder_id (CamelMapStoreSummary *map_summary,
                                              const gchar *folder_id,
                                              GError **error)
{
	gchar *ret;

	S_LOCK (map_summary);

	ret = g_key_file_get_string (
		map_summary->priv->key_file, folder_id,
		"ParentFolderId", error);

	S_UNLOCK (map_summary);

	return ret;
}

gchar *
camel_map_store_summary_get_change_key (CamelMapStoreSummary *map_summary,
                                        const gchar *folder_id,
                                        GError **error)
{
	gchar *ret;

	S_LOCK (map_summary);

	ret = g_key_file_get_string (
		map_summary->priv->key_file, folder_id,
		"ChangeKey", error);

	S_UNLOCK (map_summary);

	return ret;
}

gchar *
camel_map_store_summary_get_sync_state (CamelMapStoreSummary *map_summary,
                                        const gchar *folder_id,
                                        GError **error)
{
	gchar *ret;

	S_LOCK (map_summary);

	ret = g_key_file_get_string (
		map_summary->priv->key_file, folder_id,
		"SyncState", error);

	S_UNLOCK (map_summary);

	return ret;
}

guint64
camel_map_store_summary_get_folder_flags (CamelMapStoreSummary *map_summary,
                                          const gchar *folder_id,
                                          GError **error)
{
	guint64 ret;

	S_LOCK (map_summary);

	ret = g_key_file_get_uint64 (
		map_summary->priv->key_file, folder_id,
		"Flags", error);

	S_UNLOCK (map_summary);

	return ret;
}

guint64
camel_map_store_summary_get_folder_unread (CamelMapStoreSummary *map_summary,
                                           const gchar *folder_id,
                                           GError **error)
{
	guint64 ret;

	S_LOCK (map_summary);

	ret = g_key_file_get_uint64 (
		map_summary->priv->key_file, folder_id,
		"UnRead", error);

	S_UNLOCK (map_summary);

	return ret;
}

guint64
camel_map_store_summary_get_folder_total (CamelMapStoreSummary *map_summary,
                                          const gchar *folder_id,
                                          GError **error)
{
	guint64 ret;

	S_LOCK (map_summary);

	ret = g_key_file_get_uint64 (
		map_summary->priv->key_file, folder_id,
		"Total", error);

	S_UNLOCK (map_summary);

	return ret;
}

gchar *
camel_map_store_summary_get_string_val (CamelMapStoreSummary *map_summary,
                                         const gchar *key,
                                         GError **error)
{
	gchar *ret;

	S_LOCK (map_summary);

	ret = g_key_file_get_string (
		map_summary->priv->key_file, STORE_GROUP_NAME,
		key, error);

	S_UNLOCK (map_summary);

	return ret;
}

GSList *
camel_map_store_summary_get_folders (CamelMapStoreSummary *map_summary,
                                     const gchar *prefix)
{
	GSList *folders = NULL;
	gchar **groups = NULL;
	gsize length;
	gint prefixlen = 0;
	gint i;

	if (prefix)
		prefixlen = strlen (prefix);

	S_LOCK (map_summary);

	groups = g_key_file_get_groups (map_summary->priv->key_file, &length);

	S_UNLOCK (map_summary);

	for (i = 0; i < length; i++) {
		if (!g_ascii_strcasecmp (groups[i], STORE_GROUP_NAME))
			continue;
		if (prefixlen) {
			const gchar *fname;

			fname = g_hash_table_lookup (
				map_summary->priv->id_fname_hash, groups[i]);

			if (!fname || strncmp (fname, prefix, prefixlen) ||
			    (fname[prefixlen] && fname[prefixlen] != '/'))
				continue;
		}
		folders = g_slist_append (folders, g_strdup (groups[i]));
	}

	g_strfreev (groups);
	return folders;
}

gboolean
camel_map_store_summary_remove_folder (CamelMapStoreSummary *map_summary,
                                       const gchar *folder_id,
                                       GError **error)
{
	gboolean ret = FALSE;
	gchar *full_name;

	S_LOCK (map_summary);

	full_name = g_hash_table_lookup (map_summary->priv->id_fname_hash, folder_id);
	if (!full_name)
		goto unlock;

	ret = g_key_file_remove_group (
		map_summary->priv->key_file, folder_id, error);

	g_hash_table_remove (map_summary->priv->fname_id_hash, full_name);
	g_hash_table_remove (map_summary->priv->id_fname_hash, folder_id);

	map_summary->priv->dirty = TRUE;

 unlock:
	S_UNLOCK (map_summary);

	return ret;
}

gchar *
camel_map_store_summary_get_folder_id_from_name (CamelMapStoreSummary *map_summary,
                                                 const gchar *folder_name)
{
	gchar *folder_id;

	g_return_val_if_fail (map_summary != NULL, NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	S_LOCK (map_summary);

	folder_id = g_hash_table_lookup (map_summary->priv->fname_id_hash, folder_name);
	if (folder_id)
		folder_id = g_strdup (folder_id);

	S_UNLOCK (map_summary);

	return folder_id;
}

gchar *
camel_map_store_summary_get_folder_id_from_folder_type (CamelMapStoreSummary *map_summary,
                                                        guint64 folder_type)
{
	gchar *folder_id = NULL;
	GSList *folders, *l;

	g_return_val_if_fail (map_summary != NULL, NULL);
	g_return_val_if_fail ((folder_type & CAMEL_FOLDER_TYPE_MASK) != 0, NULL);

	folder_type = folder_type & CAMEL_FOLDER_TYPE_MASK;

	S_LOCK (map_summary);

	folders = camel_map_store_summary_get_folders (map_summary, NULL);

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		gchar *id = l->data;
		guint64 folder_flags;

		folder_flags = camel_map_store_summary_get_folder_flags (
			map_summary, id, NULL);
		if ((folder_flags & CAMEL_FOLDER_TYPE_MASK) == folder_type &&
		    (folder_flags & CAMEL_FOLDER_SYSTEM) != 0) {
			folder_id = id;
			l->data = NULL;
			break;
		}
	}

	g_slist_free_full (folders, g_free);

	S_UNLOCK (map_summary);

	return folder_id;
}

gboolean
camel_map_store_summary_has_folder (CamelMapStoreSummary *map_summary,
                                    const gchar *folder_id)
{
	gboolean ret;

	S_LOCK (map_summary);

	ret = g_key_file_has_group (map_summary->priv->key_file, folder_id);

	S_UNLOCK (map_summary);

	return ret;
}
