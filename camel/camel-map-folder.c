/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-map-folder.c: class for an map folder */

/*
 * Authors:
 *   Srinivasa Ragavan <sragavan@gnome.org>
 *
 * Copyright (C) 2012 Intel Corporation. (www.intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

/* This file is broken and suffers from multiple author syndrome.
This needs to be rewritten with a lot of functions cleaned up.
 *
There are a lot of places where code is unneccesarily duplicated,
which needs to be better organized via functions */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "utils/camel-map-settings.h"

#include "camel-map-folder.h"
#include "camel-map-store.h"
#include "camel-map-summary.h"
#include "camel-map-dbus-utils.h"

#define CAMEL_MAP_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_MAP_FOLDER, CamelMapFolderPrivate))

struct _CamelMapFolderPrivate {
	GDBusProxy *map;
	char *map_dir;
	GMutex *search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */

	/* For syncronizing refresh_info/sync_changes */
	gboolean refreshing;
	gboolean fetch_pending;
	GMutex *state_lock;
	GCond *fetch_cond;
	GHashTable *uid_eflags;
};

extern gint camel_application_is_exiting;

static gboolean map_refresh_info_sync (CamelFolder *folder, GCancellable *cancellable, GError **error);

#define d(x)

G_DEFINE_TYPE (CamelMapFolder, camel_map_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static gchar *
map_get_filename (CamelFolder *folder,
                  const gchar *uid,
                  GError **error)
{
	CamelMapFolder *map_folder = CAMEL_MAP_FOLDER (folder);
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gchar *filename;

	g_checksum_update (sha, (guchar *) uid, strlen (uid));
	filename = camel_data_cache_get_filename (
		map_folder->cache, "cur", g_checksum_get_string (sha));
	g_checksum_free (sha);

	return filename;
}

static gint
map_data_cache_remove (CamelDataCache *cdc,
                       const gchar *path,
                       const gchar *key,
                       GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gint ret;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	ret = camel_data_cache_remove (
		cdc, path, g_checksum_get_string (sha), error);
	g_checksum_free (sha);

	return ret;
}

static CamelStream *
map_data_cache_get (CamelDataCache *cdc,
                    const gchar *path,
                    const gchar *key,
                    GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	CamelStream *ret;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	ret = camel_data_cache_get (
		cdc, path, g_checksum_get_string (sha), error);
	g_checksum_free (sha);

	return ret;
}

static gchar *
map_data_cache_get_filename (CamelDataCache *cdc,
                             const gchar *path,
                             const gchar *key,
                             GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gchar *filename;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	filename = camel_data_cache_get_filename (
		cdc, path, g_checksum_get_string (sha));
	g_checksum_free (sha);

	return filename;
}

static CamelMimeMessage *
camel_map_folder_get_message_from_cache (CamelMapFolder *map_folder,
                                         const gchar *uid,
                                         GCancellable *cancellable,
                                         GError **error)
{
	CamelStream *stream;
	CamelMimeMessage *msg;
	CamelMapFolderPrivate *priv;

	priv = map_folder->priv;

	g_static_rec_mutex_lock (&priv->cache_lock);
	stream = map_data_cache_get (map_folder->cache, "cur", uid, error);
	if (!stream) {
		gchar *old_fname = camel_data_cache_get_filename (
			map_folder->cache, "cur", uid);
		if (!g_access (old_fname, R_OK)) {
			gchar *new_fname = map_data_cache_get_filename (
				map_folder->cache,
				"cur", uid, error);
			g_rename (old_fname, new_fname);
			g_free (new_fname);
			stream = map_data_cache_get (map_folder->cache, "cur", uid, error);
		}
		g_free (old_fname);
		if (!stream) {
			g_static_rec_mutex_unlock (&priv->cache_lock);
			return NULL;
		}
	}

	msg = camel_mime_message_new ();

	if (!camel_data_wrapper_construct_from_stream_sync (
		(CamelDataWrapper *) msg, stream, cancellable, error)) {
		g_object_unref (msg);
		msg = NULL;
	}

	g_static_rec_mutex_unlock (&priv->cache_lock);
	g_object_unref (stream);

	return msg;
}

static gboolean
parse_xbt_message (const char *btmsg,
		   const char *cache_file,
		   GError **error)
{
	char *bt_message = NULL;
	char *mime_message = NULL, *end;
	gsize len;
	
	if (!g_file_get_contents (btmsg, &bt_message, &len, error))
		return FALSE;

//	mime_message = bt_message;
//	mime_message = g_strstr_len (bt_message, -1, "BEGIN:MSG");
//	mime_message += strlen ("BEGIN:MSG")+1;
	end = g_strstr_len (bt_message, -1, "END:MSG");
	*end = '\0';

	if (!g_file_set_contents (cache_file, bt_message, -1, error)) {
		g_free (bt_message);
		return FALSE;
	}
	
	g_free (bt_message);
	
	return TRUE;
}

static CamelMimeMessage *
camel_map_folder_get_message (CamelFolder *folder,
                              const gchar *uid,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelMapFolder *map_folder;
	CamelMapFolderPrivate *priv;
	CamelMapStore *map_store;
	const gchar *mime_content;
	CamelMimeMessage *message = NULL;
	CamelStream *tmp_stream = NULL;
	GSList *ids = NULL, *items = NULL;
	gchar *mime_dir;
	gchar *cache_file;
	gchar *dir;
	const gchar *temp;
	gboolean res;
	gchar *mime_fname_new = NULL;
	GError *local_error = NULL;
	gchar *msg_id;
	
	map_store = (CamelMapStore *) camel_folder_get_parent_store (folder);
	map_folder = (CamelMapFolder *) folder;
	priv = map_folder->priv;

	g_mutex_lock (priv->state_lock);
	
	/* If another thread is already fetching this message, wait for it */

	/* FIXME: We might end up refetching a message anyway, if another
	 * thread has already finished fetching it by the time we get to
	 * this point in the code — map_folder_get_message_sync() doesn't
	 * hold any locks when it calls get_message_from_cache() and then
	 * falls back to this function. */
	if (g_hash_table_lookup (priv->uid_eflags, uid)) {
		do {
			g_cond_wait (priv->fetch_cond, priv->state_lock);
		} while (g_hash_table_lookup (priv->uid_eflags, uid));

		g_mutex_unlock (priv->state_lock);

		message = camel_map_folder_get_message_from_cache (map_folder, uid, cancellable, error);
		return message;
	}

	/* Because we're using this as a form of mutex, we *know* that
	 * we won't be inserting where an entry already exists. So it's
	 * OK to insert uid itself, not g_strdup (uid) */
	g_hash_table_insert (priv->uid_eflags, (gchar *) uid, (gchar *) uid);
	g_mutex_unlock (priv->state_lock);

	camel_map_store_folder_lock (map_store);
	
	ids = g_slist_append (ids, (gchar *) uid);

	mime_dir = g_build_filename (
		camel_data_cache_get_path (map_folder->cache),
		"bt-message", NULL);

	cache_file = map_data_cache_get_filename (
		map_folder->cache, "cur", uid, error);
	temp = g_strrstr (cache_file, "/");
	dir = g_strndup (cache_file, temp - cache_file);
	if (g_mkdir_with_parents (dir, 0700) == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to create cache path"));
		g_free (dir);
		g_free (cache_file);
		goto exit;
	}

	msg_id = g_strdup_printf("%s/message%s", camel_map_store_get_map_session_path(map_store), uid);
	
	if (!camel_map_store_set_current_folder (map_store, map_folder->priv->map_dir, cancellable, error)) {
		g_propagate_error (error, local_error);
		goto exit;
	}
	printf("Objid: %s\n", msg_id);
	
	res = camel_map_dbus_get_message (map_folder->priv->map,
					  msg_id,
					  mime_dir,
					  cancellable,
					  &local_error);

	if (!res) {
		g_propagate_error (error, local_error);
		goto exit;
	}

	if (!parse_xbt_message (mime_dir, cache_file, &local_error)) {
		g_propagate_error (error, local_error);
		goto exit;
	}

	message = camel_map_folder_get_message_from_cache (map_folder, uid, cancellable, error);

exit:
	camel_map_store_folder_unlock (map_store);
	
	g_mutex_lock (priv->state_lock);
	g_hash_table_remove (priv->uid_eflags, uid);
	g_mutex_unlock (priv->state_lock);
	g_cond_broadcast (priv->fetch_cond);

	if (!message && !error)
		g_set_error (
			error, CAMEL_ERROR, 1,
			"Could not retrieve the message");
	if (ids)
		g_slist_free (ids);
	if (items) {
		g_object_unref (items->data);
		g_slist_free (items);
	}

	if (tmp_stream)
		g_object_unref (tmp_stream);

	if (mime_fname_new)
		g_free (mime_fname_new);
	
	return message;
}

/* Get the message from cache if available otherwise get it from server */
static CamelMimeMessage *
map_folder_get_message_sync (CamelFolder *folder,
                             const gchar *uid,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelMimeMessage *message;

	message = camel_map_folder_get_message_from_cache ((CamelMapFolder *) folder, uid, cancellable, NULL);
	if (!message)
		message = camel_map_folder_get_message (folder, uid, cancellable, error);

	return message;
}

static CamelMimeMessage *
map_folder_get_message_cached (CamelFolder *folder,
			       const gchar *message_uid,
			       GCancellable *cancellable)
{
	return camel_map_folder_get_message_from_cache ((CamelMapFolder *) folder, message_uid, cancellable, NULL);
}

static GPtrArray *
map_folder_search_by_expression (CamelFolder *folder,
                                 const gchar *expression,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelMapFolder *map_folder;
	CamelMapFolderPrivate *priv;
	GPtrArray *matches;

	map_folder = CAMEL_MAP_FOLDER (folder);
	priv = map_folder->priv;

	g_mutex_lock (priv->search_lock);

	camel_folder_search_set_folder (map_folder->search, folder);
	matches = camel_folder_search_search (map_folder->search, expression, NULL, cancellable, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static guint32
map_folder_count_by_expression (CamelFolder *folder,
                                const gchar *expression,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelMapFolder *map_folder;
	CamelMapFolderPrivate *priv;
	guint32 matches;

	map_folder = CAMEL_MAP_FOLDER (folder);
	priv = map_folder->priv;

	g_mutex_lock (priv->search_lock);

	camel_folder_search_set_folder (map_folder->search, folder);
	matches = camel_folder_search_count (map_folder->search, expression, cancellable, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static GPtrArray *
map_folder_search_by_uids (CamelFolder *folder,
                           const gchar *expression,
                           GPtrArray *uids,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelMapFolder *map_folder;
	CamelMapFolderPrivate *priv;
	GPtrArray *matches;

	map_folder = CAMEL_MAP_FOLDER (folder);
	priv = map_folder->priv;

	if (uids->len == 0)
		return g_ptr_array_new ();

	g_mutex_lock (priv->search_lock);

	camel_folder_search_set_folder (map_folder->search, folder);
	matches = camel_folder_search_search (map_folder->search, expression, uids, cancellable, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static void
map_folder_search_free (CamelFolder *folder,
                        GPtrArray *uids)
{
	CamelMapFolder *map_folder;
	CamelMapFolderPrivate *priv;

	map_folder = CAMEL_MAP_FOLDER (folder);
	priv = map_folder->priv;

	g_return_if_fail (map_folder->search);

	g_mutex_lock (priv->search_lock);

	camel_folder_search_free_result (map_folder->search, uids);

	g_mutex_unlock (priv->search_lock);

	return;
}

static gboolean
map_synchronize_sync (CamelFolder *folder,
                      gboolean expunge,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelMapStore *map_store;

	return TRUE;
}

#if 0
static void
map_folder_count_notify_cb (CamelFolderSummary *folder_summary,
                            GParamSpec *param,
                            CamelFolder *folder)
{
	gint count;
	CamelMapStore *map_store;
	CamelMapStoreSummary *store_summary;
	gchar *folder_id;

	g_return_if_fail (folder_summary != NULL);
	g_return_if_fail (param != NULL);
	g_return_if_fail (folder != NULL);
	g_return_if_fail (folder->summary == folder_summary);

	map_store = CAMEL_MAP_STORE (camel_folder_get_parent_store (folder));
	g_return_if_fail (map_store != NULL);

	store_summary = map_store->summary;
	folder_id = camel_map_store_summary_get_folder_id_from_name (map_store->summary, camel_folder_get_full_name (folder));

	/* this can happen on folder delete/unsubscribe, after folder summary clear */
	if (!folder_id)
		return;

	if (g_strcmp0 (g_param_spec_get_name (param), "saved-count") == 0) {
		count = camel_folder_summary_get_saved_count (folder_summary);
		camel_map_store_summary_set_folder_total (store_summary, folder_id, count);
	} else if (g_strcmp0 (g_param_spec_get_name (param), "unread-count") == 0) {
		count = camel_folder_summary_get_unread_count (folder_summary);
		camel_map_store_summary_set_folder_unread (store_summary, folder_id, count);
	} else {
		g_warn_if_reached ();
	}

	g_free (folder_id);
}
#endif

CamelFolder *
camel_map_folder_new (CamelStore *store,
		      GDBusProxy *map,
		      const gchar *map_dir,
                      const gchar *folder_name,
                      const gchar *folder_dir,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelFolder *folder;
	CamelMapFolder *map_folder;
	gchar *state_file;
	const gchar *short_name;

	short_name = strrchr (folder_name, '/');
	if (!short_name)
		short_name = folder_name;
	else
		short_name++;

	folder = g_object_new (
		CAMEL_TYPE_MAP_FOLDER,
		"display_name", short_name, "full-name", folder_name,
		"parent_store", store, NULL);

	map_folder = CAMEL_MAP_FOLDER (folder);

	folder->summary = camel_map_summary_new (folder);

	if (!folder->summary) {
		g_object_unref (CAMEL_OBJECT (folder));
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load summary for %s"), folder_name);
		return NULL;
	}

	map_folder->priv->map = g_object_ref (map);
	map_folder->priv->map_dir = g_strdup (map_dir);

	/* set/load persistent state */
	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	camel_object_state_read (CAMEL_OBJECT (folder));
	g_free (state_file);

	map_folder->cache = camel_data_cache_new (folder_dir, error);
	if (!map_folder->cache) {
		g_object_unref (folder);
		return NULL;
	}

	if (!g_ascii_strcasecmp (folder_name, "Inbox")) {
		CamelSettings *settings;
		gboolean filter_inbox;

		settings = camel_service_ref_settings (CAMEL_SERVICE (store));

		filter_inbox = camel_store_settings_get_filter_inbox (
			CAMEL_STORE_SETTINGS (settings));

		if (filter_inbox)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

		g_object_unref (settings);
	}

	map_folder->search = camel_folder_search_new ();
	if (!map_folder->search) {
		g_object_unref (folder);
		return NULL;
	}

	//g_signal_connect (folder->summary, "notify::saved-count", G_CALLBACK (map_folder_count_notify_cb), folder);
	//g_signal_connect (folder->summary, "notify::unread-count", G_CALLBACK (map_folder_count_notify_cb), folder);

	return folder;
}


static gboolean
map_refresh_info_sync (CamelFolder *folder,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelMapFolder *map_folder;
	CamelMapFolderPrivate *priv;
	CamelMapStore *map_store;
	const gchar *full_name;
	gchar *id;
	gchar *sync_state;
	gboolean includes_last_item = FALSE;
	GError *local_error = NULL;
	GVariant *messages, *message, *prop, *ret;
	GVariantIter top_iter, messages_iter, message_iter, prop_iter;
	GHashTable *all_msgs;
	CamelFolderChangeInfo *ci;
	int i;
	GPtrArray *uids;
	gboolean initial_fetch;
	
	full_name = camel_folder_get_full_name (folder);
	map_store = (CamelMapStore *) camel_folder_get_parent_store (folder);

	map_folder = (CamelMapFolder *) folder;
	priv = map_folder->priv;

	g_mutex_lock (priv->state_lock);

	if (priv->refreshing) {
		g_mutex_unlock (priv->state_lock);
		return TRUE;
	}

	priv->refreshing = TRUE;
	g_mutex_unlock (priv->state_lock);

	camel_map_store_folder_lock (map_store);
	if (!camel_map_store_set_current_folder (map_store, "/telecom/msg", cancellable, error)) {
		camel_map_store_folder_unlock (map_store);
		g_mutex_lock (priv->state_lock);
		priv->refreshing = FALSE;
		g_mutex_unlock (priv->state_lock);

		return FALSE;
	}

	camel_folder_summary_prepare_fetch_all (folder->summary, NULL);

	ret = camel_map_dbus_get_message_listing (map_folder->priv->map,
			full_name,
			cancellable,
			&local_error);
	if (ret == NULL || local_error) {
		printf("Unable to refresh folder: %s\n", local_error ? local_error->message : "Empty error msg");
		g_propagate_error (error, local_error);
		camel_map_store_folder_unlock (map_store);
		g_mutex_lock (priv->state_lock);
		priv->refreshing = FALSE;
		g_mutex_unlock (priv->state_lock);

		return FALSE;
	}

	/* Only during initial fetch, we can check for deleted messages */
	initial_fetch = camel_map_store_get_initial_fetch(map_store);
	if (initial_fetch)
		camel_map_store_set_initial_fetch (map_store, FALSE);
	
	/* DBus message structure: a{oa{sv}} */
	all_msgs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	ci = camel_folder_change_info_new ();
	
	g_variant_iter_init (&top_iter, ret);
	while ((messages = g_variant_iter_next_value (&top_iter))) {
	
		g_variant_iter_init (&messages_iter, messages);
		while ((message = g_variant_iter_next_value (&messages_iter))) {
			char *msg_obj=NULL;
			char *uid;
			CamelMessageInfoBase *info;
			GVariant *item;
			GVariant *value;
			char *key;
			
			g_variant_get (message,
				       "{&o@a{sv}}",
				       &msg_obj,
				       &prop);
			uid = g_strrstr(msg_obj, "/");
			uid += strlen("message")+1;
			printf("Message: %s: %s \t\t %s\n", msg_obj, uid, g_variant_print (prop, TRUE));
			g_hash_table_insert (all_msgs, uid, msg_obj);
			
			info = camel_folder_summary_get (folder->summary, uid);
			if (info) {
				GVariant *item;
				GVariant *value;
				char *key;
				CamelMessageFlags flags=0;
				gboolean changed = FALSE;
				
				/*Check if the message changed */
				/*
				   Message Format across dbus 
				   {
				     objectpath '/org/bluez/obex/session5/message2147483650',
				     {
				       'Protected': <false>,
				       'Read': <true>,
				       'Priority': <false>,
				       'Status': <'complete'>,
				       'Size': <uint64 39>,
				       'Type': <'EMAIL'>,
				       'RecipientAddress': <'
				                           meegotabletmail@gmail.com;
							   sragavan@gmail.com;
							   srinivasa.ragavan.venkateswaran@intel.com'
							   >,
					'Recipient': <'
					             meegotabletmail;
						     Srini;
						     Srinivasa Ragavan Venkateswaran
						     '>,
					'SenderAddress': <'sragavan@gmail.com'>,
					'Sender': <'Srini'>,
					'Timestamp': <'20120913T175106'>,
					'Subject': <'Multiple recipients'>
				      }
				    }
				*/
				g_variant_iter_init (&prop_iter, prop);				
				//g_variant_get (prop, "a{sv}", &prop_iter);
				while ((item = g_variant_iter_next_value (&prop_iter))) {
					g_variant_get (item, "{sv}", &key, &value);

					if (strcmp (key, "Read") == 0) {
						if (g_variant_get_boolean (value))
							flags |= CAMEL_MESSAGE_SEEN; 
					} else if (strcmp (key, "Priority") == 0) {
						if (g_variant_get_boolean (value))
							flags |= CAMEL_MESSAGE_FLAGGED;
					}
					g_free (key);
					g_variant_unref (value);
					g_variant_unref (item);
				}

				if (((info->flags & CAMEL_MESSAGE_SEEN) != 0) !=  ((flags & CAMEL_MESSAGE_SEEN) != 0)) {
					changed = TRUE;
					camel_message_info_set_flags ((CamelMessageInfo *)info, CAMEL_MESSAGE_SEEN, (flags & CAMEL_MESSAGE_SEEN));
				}

				if (((info->flags & CAMEL_MESSAGE_FLAGGED) != 0) != ((flags & CAMEL_MESSAGE_FLAGGED) != 0)) {
					changed = TRUE;
					camel_message_info_set_flags ((CamelMessageInfo *)info, CAMEL_MESSAGE_FLAGGED, (flags & CAMEL_MESSAGE_FLAGGED));
				}

				if (changed) {
					camel_folder_change_info_change_uid (ci, uid);
				}
			} else {
				char *to_name = NULL, *to_email = NULL, *from_name = NULL, *from_email = NULL ;
				/* Its a new message, lets add it to summary */
				info = camel_message_info_new (folder->summary);
				info->uid = camel_pstring_strdup (uid);
				if (info->content == NULL) {
					info->content =
						camel_folder_summary_content_info_new (
							folder->summary);
					info->content->type =
						camel_content_type_new ("multipart", "mixed");
				}
				g_variant_iter_init (&prop_iter, prop);
				//g_variant_get (prop, "a{sv}", &prop_iter);
				while ((item = g_variant_iter_next_value (&prop_iter))) {
					g_variant_get (item, "{sv}", &key, &value);

					printf("Processing key: %s\n", key);

					if (strcmp (key, "Read") == 0) {
						if (g_variant_get_boolean (value))
							camel_message_info_set_flags ((CamelMessageInfo *)info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
						else
							camel_message_info_set_flags ((CamelMessageInfo *)info, CAMEL_MESSAGE_SEEN, 0);
											      
					} else if (strcmp (key, "Priority") == 0) {
						if (g_variant_get_boolean (value))
							camel_message_info_set_flags ((CamelMessageInfo *)info, CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED);
						else
							camel_message_info_set_flags ((CamelMessageInfo *)info, CAMEL_MESSAGE_FLAGGED, 0);
					} else if (strcmp (key, "Size") == 0) {
						info->size = (guint32) g_variant_get_uint64 (value);
					} else if (strcmp (key, "RecipientAddress") == 0) {
						to_email = g_variant_get_string (value, NULL);
					} else if (strcmp (key, "Recipient") == 0) {
						to_name = g_variant_get_string (value, NULL);
					} else if (strcmp (key, "SenderAddress") == 0) {
						from_email = g_variant_get_string (value, NULL);
					} else if (strcmp (key, "Sender") == 0) {
						from_name = g_variant_get_string (value, NULL);
					} else if (strcmp (key, "Timestamp") == 0) {
						const char *tstr = g_variant_get_string (value, NULL);
						GTimeVal val;

						g_time_val_from_iso8601 (tstr, &val);
						info->date_received = val.tv_sec;
						info->date_sent = val.tv_sec;
					} else if (strcmp (key, "Subject") == 0) {
						info->subject = camel_pstring_strdup (g_variant_get_string (value, NULL));
					}

					if (from_name || from_email) {
						/* We have se*/
						GString *str = g_string_new ("");
						if (from_name) {
							g_string_append (str, from_name);
							g_string_append (str, " ");
						}
						if (from_email) {
							g_string_append (str, "<");
							g_string_append (str, from_email);
							g_string_append (str, ">");
						}

						info->from = camel_pstring_add (g_string_free (str, FALSE), TRUE);
							
					} else {
						info->from = camel_pstring_strdup ("");
					}

					if ((to_name && *to_name) || (to_email && to_email)) {
						char **names_to = NULL;
						char **emails_to = NULL;
						GString *str = NULL;

						i=0;
						names_to = (to_name && *to_name) ? g_strsplit (to_name, ";", 0) : NULL;
						emails_to = (to_email && *to_email) ? g_strsplit (to_email, ";", 0) : NULL;
						while ((names_to && names_to[i] != NULL) || (emails_to &&  emails_to[i] != NULL)) {

							if (!str)
								str = g_string_new ("");
							else
								g_string_append (str, ", ");

							if (names_to && names_to[i]) {
								g_string_append (str, names_to[i]);
								g_string_append (str, " ");
							}
							if (emails_to && emails_to[i]) {
								g_string_append (str, "<");
								g_string_append (str, emails_to[i]);
								g_string_append (str, ">");
							}
							i++;
						}

						info->to = camel_pstring_add (g_string_free (str, FALSE), TRUE);
						
					}

					info->cc = NULL;
					g_free (key);
					g_variant_unref (value);
					g_variant_unref (item);

					camel_folder_summary_add (
						folder->summary, (CamelMessageInfo *) info);
					info->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
					camel_folder_change_info_add_uid (ci, uid);
					camel_folder_change_info_recent_uid (ci, uid);
					
				}
				
			}
			g_variant_unref (message);
			g_variant_unref (prop);
		}
		g_variant_unref (messages);
	}

	/* Check for deleted messages */
	if (initial_fetch) {
		uids = camel_folder_summary_get_array (folder->summary);
		for (i = 0; i < uids->len; i++) {
			if (!g_hash_table_lookup (all_msgs, uids->pdata[i])) {
				camel_folder_summary_remove_uid (folder->summary, uids->pdata[i]);
				camel_folder_change_info_remove_uid (ci, uids->pdata[i]);
			}
		}
		camel_folder_summary_free_array (uids);
	}
	
	
	
//		total = camel_folder_summary_count (folder->summary);
//		unread = camel_folder_summary_get_unread_count (folder->summary);

//		camel_map_store_summary_set_folder_total (map_store->summary, id, total);
//		camel_map_store_summary_set_folder_unread (map_store->summary, id, unread);
//		camel_map_store_summary_save (map_store->summary, NULL);
	if (camel_folder_change_info_changed (ci)) {
		camel_folder_summary_touch (folder->summary);
		camel_folder_summary_save_to_db (folder->summary, NULL);
		camel_folder_changed (folder, ci);
	}
	camel_folder_change_info_free (ci);

	if (local_error)
		g_propagate_error (error, local_error);

	camel_map_store_folder_unlock (map_store);

	
	g_mutex_lock (priv->state_lock);
	priv->refreshing = FALSE;
	g_mutex_unlock (priv->state_lock);


	return !local_error;
}

static gboolean
map_append_message_sync (CamelFolder *folder,
                         CamelMimeMessage *message,
                         CamelMessageInfo *info,
                         gchar **appended_uid,
                         GCancellable *cancellable,
                         GError **error)
{
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Cannot append message. MAP doesn't support it."));

	return FALSE;
}

/* move messages */
static gboolean
map_transfer_messages_to_sync (CamelFolder *source,
                               GPtrArray *uids,
                               CamelFolder *destination,
                               gboolean delete_originals,
                               GPtrArray **transferred_uids,
                               GCancellable *cancellable,
                               GError **error)
{
	/* MAP doesn't support message transfer*/
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Cannot transfer messages. MAP doesn't support it."));


	return FALSE;
}

static gboolean
map_expunge_sync (CamelFolder *folder,
                  GCancellable *cancellable,
                  GError **error)
{

	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Cannot expunge folder '%s'. MAP doesn't support"),
		camel_folder_get_full_name (folder));

	return FALSE;
}

static gint
map_cmp_uids (CamelFolder *folder,
              const gchar *uid1,
              const gchar *uid2)
{
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	return strcmp (uid1, uid2);
}

static void
map_folder_dispose (GObject *object)
{
	CamelMapFolder *map_folder = CAMEL_MAP_FOLDER (object);

	if (map_folder->cache != NULL) {
		g_object_unref (map_folder->cache);
		map_folder->cache = NULL;
	}

	if (map_folder->search != NULL) {
		g_object_unref (map_folder->search);
		map_folder->search = NULL;
	}

	g_object_unref (map_folder->priv->map);
	g_free (map_folder->priv->map_dir);

	g_mutex_free (map_folder->priv->search_lock);
	g_hash_table_destroy (map_folder->priv->uid_eflags);
	g_cond_free (map_folder->priv->fetch_cond);

//	if (CAMEL_FOLDER (map_folder)->summary)
//		g_signal_handlers_disconnect_by_func (CAMEL_FOLDER (map_folder)->summary, G_CALLBACK (map_folder_count_notify_cb), map_folder);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_map_folder_parent_class)->dispose (object);
}

static void
map_folder_constructed (GObject *object)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelStore *parent_store;
	CamelService *service;
	CamelFolder *folder;
	const gchar *full_name;
	gchar *description;
	const gchar *host;
	gchar *user;

	folder = CAMEL_FOLDER (object);
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	service = CAMEL_SERVICE (parent_store);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_map_settings_get_device_name ((CamelMapSettings *)settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	description = g_strdup_printf (
		"%s@%s:%s", user, host, full_name);
	camel_folder_set_description (folder, description);
	g_free (description);

	g_free (user);
}

static void
camel_map_folder_class_init (CamelMapFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	g_type_class_add_private (class, sizeof (CamelMapFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = map_folder_dispose;
	object_class->constructed = map_folder_constructed;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->get_message_sync = map_folder_get_message_sync;
	folder_class->get_message_cached = map_folder_get_message_cached;
	folder_class->search_by_expression = map_folder_search_by_expression;
	folder_class->count_by_expression = map_folder_count_by_expression;
	folder_class->cmp_uids = map_cmp_uids;
	folder_class->search_by_uids = map_folder_search_by_uids;
	folder_class->search_free = map_folder_search_free;
	folder_class->append_message_sync = map_append_message_sync;
	folder_class->refresh_info_sync = map_refresh_info_sync;
	folder_class->synchronize_sync = map_synchronize_sync;
	folder_class->expunge_sync = map_expunge_sync;
	folder_class->transfer_messages_to_sync = map_transfer_messages_to_sync;
	folder_class->get_filename = map_get_filename;
}

static void
camel_map_folder_init (CamelMapFolder *map_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (map_folder);

	map_folder->priv = CAMEL_MAP_FOLDER_GET_PRIVATE (map_folder);

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN |
		CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_USER;

	folder->folder_flags = CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;

	map_folder->priv->search_lock = g_mutex_new ();
	map_folder->priv->state_lock = g_mutex_new ();
	g_static_rec_mutex_init (&map_folder->priv->cache_lock);

	map_folder->priv->refreshing = FALSE;

	map_folder->priv->fetch_cond = g_cond_new ();
	map_folder->priv->uid_eflags = g_hash_table_new (g_str_hash, g_str_equal);
	camel_folder_set_lock_async (folder, TRUE);
}

void
camel_map_folder_mark_message_read (CamelMapFolder *map_folder,
				    const char *uid,
				    gboolean read)
{
	CamelMapFolderPrivate *priv;
	CamelMapStore *map_store;
	gboolean res;
	GError *local_error = NULL;
	gchar *msg_id;
	
	map_store = (CamelMapStore *) camel_folder_get_parent_store ((CamelFolder *)map_folder);

	priv = map_folder->priv;


	camel_map_store_folder_lock (map_store);
	
	msg_id = g_strdup_printf("%s/message%s", camel_map_store_get_map_session_path(map_store), uid);
	
	if (!camel_map_store_set_current_folder (map_store, map_folder->priv->map_dir, NULL, &local_error)) {
		goto exit;
	}
	printf("Objid: %s\n", msg_id);
	
	res = camel_map_dbus_set_message_read (map_folder->priv->map,
					       msg_id,
					       read,
					       NULL,
					       &local_error);

	if (local_error) {
		goto exit;
	}

exit:
	camel_map_store_folder_unlock (map_store);	

	return;	
}

void
camel_map_folder_mark_message_deleted (CamelMapFolder *map_folder,
				       const char *uid,
				       gboolean deleted)
{
	CamelMapFolderPrivate *priv;
	CamelMapStore *map_store;
	gboolean res;

	GError *local_error = NULL;
	gchar *msg_id;
	
	map_store = (CamelMapStore *) camel_folder_get_parent_store ((CamelFolder *)map_folder);

	priv = map_folder->priv;


	camel_map_store_folder_lock (map_store);
	
	msg_id = g_strdup_printf("%s/message%s", camel_map_store_get_map_session_path(map_store), uid);
	
	if (!camel_map_store_set_current_folder (map_store, map_folder->priv->map_dir, NULL, &local_error)) {
		goto exit;
	}
	printf("Objid: %s\n", msg_id);
	
	res = camel_map_dbus_set_message_deleted (map_folder->priv->map,
						  msg_id,
						  deleted,
						  NULL,
						  &local_error);

	if (local_error) {
		goto exit;
	}

exit:
	camel_map_store_folder_unlock (map_store);	

	return;	
}
/** End **/
