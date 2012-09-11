/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-map-store.c : class for an map store */

/*
 *  Authors:
 *  Srinivasa Ragavan <sragavan@gnome.org>
 *
 *  Copyright (C) 2012 Intel Corporation. (www.intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

//#include "camel-map-folder.h"
#include <camel/camel.h>
#include "camel-map-store.h"
#include "camel-map-dbus-utils.h"
#include "utils/camel-map-settings.h"
//#include "camel-map-summary.h"
//#include "camel-map-utils.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define d(x) x

#define CAMEL_MAP_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_MAP_STORE, CamelMapStorePrivate))

#define FINFO_REFRESH_INTERVAL 60

struct _CamelMapStorePrivate {
	char *session_path;
	GDBusProxy *session;
	GDBusProxy *map;
	GDBusConnection *connection;	
	time_t last_refresh_time;
	GMutex *get_finfo_lock;
	GMutex *connection_lock;
};

static gboolean	map_store_construct	(CamelService *service, CamelSession *session,
					 CamelProvider *provider, GError **error);

static void camel_map_store_initable_init (GInitableIface *interface);
static void camel_map_subscribable_init (CamelSubscribableInterface *interface);
static GInitableIface *parent_initable_interface;

G_DEFINE_TYPE_WITH_CODE (
	CamelMapStore, camel_map_store, CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE, camel_map_store_initable_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_SUBSCRIBABLE, camel_map_subscribable_init))


static gboolean
map_store_initable_init (GInitable *initable,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelService *service;
	CamelSession *session;
	CamelStore *store;
	gboolean ret;

	store = CAMEL_STORE (initable);
	service = CAMEL_SERVICE (initable);
	session = camel_service_get_session (service);

	store->flags |= CAMEL_STORE_USE_CACHE_DIR;

	/* Chain up to parent interface's init() method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	ret = map_store_construct (service, session, NULL, error);

	/* Add transport here ? */

	return ret;
}

static void
camel_map_store_initable_init (GInitableIface *interface)
{
	parent_initable_interface = g_type_interface_peek_parent (interface);

	interface->init = map_store_initable_init;
}

static gboolean
map_store_construct (CamelService *service,
                     CamelSession *session,
                     CamelProvider *provider,
                     GError **error)
{
	CamelMapStore *map_store;
	gchar *summary_file, *session_storage_path;

	map_store = (CamelMapStore *) service;

	/*storage path*/
	session_storage_path = g_strdup (camel_service_get_user_cache_dir (service));
	if (!session_storage_path) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Session has no storage path"));
		return FALSE;
	}
	map_store->storage_path = session_storage_path;

	/* Note. update account-listener plugin if filename is changed here, as it would remove the summary
	 * by forming the path itself */
	g_mkdir_with_parents (map_store->storage_path, 0700);
	summary_file = g_build_filename (map_store->storage_path, "folder-tree", NULL);
	map_store->summary = camel_map_store_summary_new (summary_file);
	camel_map_store_summary_load (map_store->summary, NULL);

	g_free (summary_file);
	return TRUE;
}


static void
map_update_folder_hierarchy (CamelMapStore *map_store,
                             gchar *sync_state,
                             gboolean includes_last_folder,
                             GSList *folders_created,
                             GSList *folders_deleted,
                             GSList *folders_updated)
{
	//map_utils_sync_folders (map_store, folders_created, folders_deleted, folders_updated);

	camel_map_store_summary_store_string_val (map_store->summary, "sync_state", sync_state);
	camel_map_store_summary_save (map_store->summary, NULL);

	g_slist_foreach (folders_created, (GFunc) g_object_unref, NULL);
	g_slist_foreach (folders_updated, (GFunc) g_object_unref, NULL);
	g_slist_foreach (folders_deleted, (GFunc) g_free, NULL);
	g_slist_free (folders_created);
	g_slist_free (folders_deleted);
	g_slist_free (folders_updated);
	g_free (sync_state);
}

static void
transfer_complete (GDBusConnection *connection,
		   const gchar *sender_name,
		   const gchar *object_path,
		   const gchar *interface_name,
		   const gchar *signal_name,
		   GVariant *parameters,
		   gpointer user_data)
{
	printf("%s: complete", signal_name);
}


static void                
transfer_error (GDBusConnection *connection,
		const gchar *sender_name,
		const gchar *object_path,
		const gchar *interface_name,
		const gchar *signal_name,
		GVariant *parameters,
		gpointer user_data)
{
	printf("%s: error\n", signal_name);
}

static gboolean
map_connect_sync (CamelService *service,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelMapStore *map_store;
	CamelSession *session;
	gboolean success;
	CamelSettings *settings;
	CamelMapSettings *map_settings;
	GVariant *ret;
	
	map_store = CAMEL_MAP_STORE (service);
	session = camel_service_get_session (service);
	
	if (map_store->priv->session_path) /* Already connected */
		return TRUE;

	if (map_store->priv->connection == NULL) {
		map_store->priv->connection = camel_map_connect_dbus (cancellable, error);
	}

	if (map_store->priv->connection == NULL)
		return FALSE;

	settings = camel_service_ref_settings (service);
	map_settings = CAMEL_MAP_SETTINGS(settings);

	map_store->priv->session_path = camel_map_connect_device_channel (map_store->priv->connection,
					camel_map_settings_get_device_str_address (map_settings),
					camel_map_settings_get_channel (map_settings),
					cancellable,
					error);
	
	if (!map_store->priv->session_path)
		return FALSE;
	
	map_store->priv->session = g_dbus_proxy_new_sync (map_store->priv->connection,
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					"org.bluez.obex.client",
					map_store->priv->session_path,
					"org.bluez.obex.Session",
					cancellable,
					error);
	map_store->priv->map = g_dbus_proxy_new_sync (map_store->priv->connection,
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					"org.bluez.obex.client",
					map_store->priv->session_path,
					"org.bluez.obex.MessageAccess",
					cancellable,
					error);

	g_dbus_connection_signal_subscribe (map_store->priv->connection,
			NULL,
			"org.bluez.obex.Transfer",
			"Complete",
			map_store->priv->session_path,
			NULL,
			G_DBUS_SIGNAL_FLAGS_NONE,
			transfer_complete,
			NULL,
			NULL);

	g_dbus_connection_signal_subscribe (map_store->priv->connection,
			NULL,
			"org.bluez.obex.Transfer",
			"Error",
			map_store->priv->session_path,
			NULL,
			G_DBUS_SIGNAL_FLAGS_NONE,
			transfer_error,
			NULL,
			NULL);

	ret = camel_map_dbus_set_current_folder (map_store->priv->map,
						 "telecom/msg",
						 cancellable,
						 error);
	if (ret == NULL) {
		printf("Set folder to telecom/msg failed\n");
		return FALSE;
	}

	printf("SETFOLDER: %s\n", g_variant_print (ret, TRUE));

	ret = camel_map_dbus_get_folder_listing (map_store->priv->map,
						 cancellable,
						 error);

	if (ret == NULL) {
		printf("Getfolderlist error\n");
		return FALSE;
	}
	printf("GetFolderList: %s\n", g_variant_print (ret, TRUE));

	camel_offline_store_set_online_sync (
		CAMEL_OFFLINE_STORE (map_store),
		TRUE, cancellable, NULL);

	return success;
}

static gboolean
map_disconnect_sync (CamelService *service,
                     gboolean clean,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelMapStore *map_store = (CamelMapStore *) service;
	CamelServiceClass *service_class;

#if 0
	g_mutex_lock (map_store->priv->connection_lock);

	/* TODO cancel all operations in the connection */
	if (map_store->priv->connection != NULL) {
		CamelSettings *settings;

		/* FIXME This is somewhat broken, since the CamelSettings
		 *       instance returned here may not be the same instance
		 *       that we connected a signal handler to.  Need to keep
		 *       our own reference to that CamelSettings instance, or
		 *       better yet avoid connecting signal handlers to it in
		 *       the first place. */
		settings = camel_service_ref_settings (service);
		g_signal_handlers_disconnect_by_data (settings, service);
		g_object_unref (settings);

		e_map_connection_set_password (
			map_store->priv->connection, NULL);
		g_object_unref (map_store->priv->connection);
		map_store->priv->connection = NULL;
	}

	g_mutex_unlock (map_store->priv->connection_lock);
#endif
	service_class = CAMEL_SERVICE_CLASS (camel_map_store_parent_class);
	return service_class->disconnect_sync (service, clean, cancellable, error);
}

typedef struct {
	const gchar *dist_folder_id;
	gint info_flags;
} SystemFolder;

static SystemFolder system_folder[] = {
	{"calendar", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_EVENTS},
	{"contacts", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_CONTACTS},
	{"deleteditems", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_TRASH},
	{"drafts", CAMEL_FOLDER_SYSTEM},
	{"inbox", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX},
	{"journal", CAMEL_FOLDER_SYSTEM | CAMEL_MAP_FOLDER_TYPE_JOURNAL},
	{"notes", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_MEMOS},
	{"outbox", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_OUTBOX},
	{"sentitems", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_SENT},
	{"tasks", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_TASKS},
	{"msgfolderroot", CAMEL_FOLDER_SYSTEM},
	{"root", CAMEL_FOLDER_SYSTEM},
	{"junkemail", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_JUNK},
	{"searchfolders", CAMEL_FOLDER_SYSTEM},
};

#if 0
static void
map_store_set_flags (CamelMapStore *map_store,
                     GSList *folders)
{
	GSList *temp = NULL;
	EMapFolder *folder = NULL;
	const MapFolderId *fid = NULL;
	gint n = 0;

	temp = folders;
	while (temp != NULL) {
		folder = (EMapFolder *) temp->data;
		fid = e_map_folder_get_id (folder);

		if (camel_map_store_summary_has_folder (map_store->summary, fid->id))
			camel_map_store_summary_set_folder_flags (map_store->summary, fid->id, system_folder[n].info_flags);

		temp = temp->next;
		n++;
	}
}
#endif

static CamelAuthenticationResult
map_authenticate_sync (CamelService *service,
                       const gchar *mechanism,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelAuthenticationResult result;
	CamelMapStore *map_store;
	CamelSettings *settings;
	CamelMapSettings *map_settings;
	GSList *folders_created = NULL;
	GSList *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	GSList *folder_ids = NULL, *folders = NULL;
	gboolean includes_last_folder = FALSE;
	gboolean initial_setup = FALSE;
	const gchar *password;
	gchar *hosturl;
	gchar *sync_state = NULL;
	GError *local_error = NULL, *folder_err = NULL;
	gint n = 0;

#if 0	
	map_store = CAMEL_MAP_STORE (service);

	password = camel_service_get_password (service);

	if (password == NULL) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Authentication password not available"));
		return CAMEL_AUTHENTICATION_ERROR;
	}

	settings = camel_service_ref_settings (service);

	map_settings = CAMEL_MAP_SETTINGS (settings);
	hosturl = camel_map_settings_dup_hosturl (map_settings);

	connection = e_map_connection_new (hosturl, map_settings);
	e_map_connection_set_password (connection, password);

	g_free (hosturl);

	g_object_unref (settings);

	/* XXX We need to run some operation that requires authentication
	 *     but does not change any server-side state, so we can check
	 *     the error status and determine if our password is valid.
	 *     David suggested e_map_connection_sync_folder_hierarchy(),
	 *     since we have to do that eventually anyway. */

	/*use old sync_state from summary*/
	sync_state = camel_map_store_summary_get_string_val (map_store->summary, "sync_state", NULL);
	if (!sync_state)
		initial_setup = TRUE;

	e_map_connection_sync_folder_hierarchy_sync (
		connection, MAP_PRIORITY_MEDIUM,
		&sync_state, &includes_last_folder,
		&folders_created, &folders_updated, &folders_deleted,
		cancellable, &local_error);

	if (local_error == NULL) {
		g_mutex_lock (map_store->priv->connection_lock);
		if (map_store->priv->connection != NULL)
			g_object_unref (map_store->priv->connection);
		map_store->priv->connection = g_object_ref (connection);
		g_mutex_unlock (map_store->priv->connection_lock);

		/* This consumes all allocated result data. */
		map_update_folder_hierarchy (
			map_store, sync_state, includes_last_folder,
			folders_created, folders_deleted, folders_updated);
	} else {
		g_mutex_lock (map_store->priv->connection_lock);
		if (map_store->priv->connection != NULL) {
			g_object_unref (map_store->priv->connection);
			map_store->priv->connection = NULL;
		}
		g_mutex_unlock (map_store->priv->connection_lock);

		g_free (sync_state);

		/* Make sure we're not leaking anything. */
		g_warn_if_fail (folders_created == NULL);
		g_warn_if_fail (folders_updated == NULL);
		g_warn_if_fail (folders_deleted == NULL);
	}

	/*get folders using distinguished id by GetFolder operation and set system flags to folders, only for first time*/
	if (initial_setup && local_error == NULL) {
		while (n < G_N_ELEMENTS (system_folder)) {
			MapFolderId *fid = NULL;

			fid = g_new0 (MapFolderId, 1);
			fid->id = g_strdup (system_folder[n].dist_folder_id);
			fid->is_distinguished_id = TRUE;
			folder_ids = g_slist_append (folder_ids, fid);
			n++;
		}

		/* fetch system folders first using getfolder operation*/
		e_map_connection_get_folder_sync (
			connection, MAP_PRIORITY_MEDIUM, "IdOnly",
			NULL, folder_ids, &folders,
			cancellable, &folder_err);

		if (g_slist_length (folders) && (g_slist_length (folders) != G_N_ELEMENTS (system_folder)))
			d (printf ("Error : not all folders are returned by getfolder operation"));
		else if (folder_err == NULL && folders != NULL)
			map_store_set_flags (map_store, folders);
		else if (folder_err) {
			/*report error and make sure we are not leaking anything*/
			g_warn_if_fail (folders == NULL);
		} else
			d (printf ("folders for respective distinguished ids don't exist"));

		g_slist_foreach (folders, (GFunc) g_object_unref, NULL);
		g_slist_foreach (folder_ids, (GFunc) e_map_folder_id_free, NULL);
		g_slist_free (folders);
		g_slist_free (folder_ids);
		g_clear_error (&folder_err);
	}

	if (local_error == NULL) {
		result = CAMEL_AUTHENTICATION_ACCEPTED;
	} else if (g_error_matches (local_error, MAP_CONNECTION_ERROR, MAP_CONNECTION_ERROR_AUTHENTICATION_FAILED)) {
		g_clear_error (&local_error);
		result = CAMEL_AUTHENTICATION_REJECTED;
	} else {
		g_propagate_error (error, local_error);
		result = CAMEL_AUTHENTICATION_ERROR;
	}

	g_object_unref (connection);
#endif

	return result;
}

static  GList *
map_store_query_auth_types_sync (CamelService *service,
                                 GCancellable *cancellable,
                                 GError **error)
{
	g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Query for authentication types is not supported"));

	return NULL;
}

static CamelFolderInfo * map_create_folder_sync (CamelStore *store, const gchar *parent_name,const gchar *folder_name, GCancellable *cancellable, GError **error);

static CamelFolder *
map_get_folder_sync (CamelStore *store,
                     const gchar *folder_name,
                     guint32 flags,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelMapStore *map_store;
	CamelFolder *folder = NULL;
	gchar *fid, *folder_dir;
#if 0
	map_store = (CamelMapStore *) store;

	fid = camel_map_store_summary_get_folder_id_from_name (map_store->summary, folder_name);

	/* We don't support CAMEL_STORE_FOLDER_EXCL. Nobody ever uses it */
	if (!fid && (flags & CAMEL_STORE_FOLDER_CREATE)) {
		CamelFolderInfo *fi;
		const gchar *parent, *top, *slash;
		gchar *copy = NULL;

		slash = strrchr (folder_name, '/');
		if (slash) {
			copy = g_strdup (folder_name);

			/* Split into parent path, and new name */
			copy[slash - folder_name] = 0;
			parent = copy;
			top = copy + (slash - folder_name) + 1;
		} else {
			parent = "";
			top = folder_name;
		}

		fi = map_create_folder_sync (store, parent, top, cancellable, error);
		g_free (copy);

		if (!fi)
			return NULL;

		camel_folder_info_free (fi);
	} else if (!fid) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_ERROR_GENERIC,
			_("No such folder: %s"), folder_name);
		return NULL;
	} else {
		/* We don't actually care what it is; only that it exists */
		g_free (fid);
	}

	folder_dir = g_build_filename (map_store->storage_path, "folders", folder_name, NULL);
	folder = camel_map_folder_new (store, folder_name, folder_dir, cancellable, error);

	g_free (folder_dir);
#endif
	return folder;
}

static CamelFolderInfo *
folder_info_from_store_summary (CamelMapStore *store,
                                const gchar *top,
                                guint32 flags,
                                GError **error)
{
	CamelMapStoreSummary *map_summary;
	GSList *folders, *l;
	GPtrArray *folder_infos;
	CamelFolderInfo *root_fi = NULL;
#if 0
	map_summary = store->summary;
	folders = camel_map_store_summary_get_folders (map_summary, top);

	if (!folders)
		return NULL;

	folder_infos = g_ptr_array_new ();

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		CamelFolderInfo *fi;
		EMapFolderType ftype;

		ftype = camel_map_store_summary_get_folder_type (map_summary, l->data, NULL);
		if (ftype != E_MAP_FOLDER_TYPE_MAILBOX)
			continue;

		fi = camel_map_utils_build_folder_info (store, l->data);
		g_ptr_array_add (folder_infos, fi);
	}

	root_fi = camel_folder_info_build (folder_infos, top, '/', TRUE);

	g_ptr_array_free (folder_infos, TRUE);
	g_slist_foreach (folders, (GFunc) g_free, NULL);
	g_slist_free (folders);
#endif
	return root_fi;
}


static void
create_folder_hierarchy (CamelStore *store,
			 const char *parent,
			 CamelFolderInfo **fiparent,
			 GHashTable *table,
			 GCancellable *cancellable,
			 GError **error)
{
	GVariant *folders;
	GVariant *child, *subchild, *tchild, *ret;
	GVariantIter iter, subiter, titer;
	CamelMapStore *map_store;
	CamelFolderInfo *fi = NULL, *parent_fi;
	
	map_store = (CamelMapStore *) store;
	parent_fi = (CamelFolderInfo *)*fiparent;

	ret = camel_map_dbus_set_current_folder (map_store->priv->map,
						 parent,
						 cancellable,
						 error);
	if (ret == NULL) {
		printf("Set folder to %s failed\n", parent);
		return;
	}

	folders = camel_map_dbus_get_folder_listing (map_store->priv->map, 
			cancellable, error);
	if (folders == NULL) {
		printf("Unable to get folder listing in: %s\n", parent);
		return;
	}

	g_variant_iter_init (&iter, folders);
	while ((child = g_variant_iter_next_value (&iter))) {
		g_variant_iter_init (&subiter, child);
		while ((subchild = g_variant_iter_next_value (&subiter))) {
			g_variant_iter_init (&titer, subchild);		
			while ((tchild = g_variant_iter_next_value (&titer))) {
				gchar *name, *folder, *newfolder;
				GVariant *value;
				
      				g_variant_get (tchild,
       	        	                   "{sv}",
	               	                   &name,
					   &value);
				folder = g_variant_get_string (value, NULL);

				if (!folder || !*folder || strcmp (folder, "msg") == 0)
					continue;
				newfolder = g_strdup_printf("%s/%s", parent, folder);
				g_hash_table_insert (table, newfolder, newfolder);
				if (fi == NULL) {
					/* First FI in this hierarchy */
					fi = camel_folder_info_new ();
					if (parent_fi == NULL) {
						/* We are exploring the root dir */
						*fiparent = fi;
					} else {
						/* Link upto the parent FI */
						fi->parent = parent_fi;
					}
				} else {
					CamelFolderInfo *info = camel_folder_info_new ();
		
					info->parent = parent_fi;
					fi->next = info;
					fi = info;
				}

				fi->full_name = g_strdup (newfolder+strlen("/telecom/msg/"));
				if (!g_ascii_strcasecmp(fi->full_name, "inbox")) {
					fi->display_name = g_strdup (_("Inbox"));
					fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_INBOX;
					fi->flags |= CAMEL_FOLDER_SYSTEM;
				} else
					fi->display_name = g_strdup (folder);
				/* Add the tree to store summary if not there already */
				/* Parse the subtree now */
				create_folder_hierarchy (store, newfolder, &fi, table, cancellable, error);
			
				g_free (name);
				g_variant_unref (value);
				g_variant_unref (tchild);
		    	}
			g_variant_unref (subchild);
		}
		g_variant_unref (child);
	}
}

static CamelFolderInfo *
map_get_folder_info_sync (CamelStore *store,
                          const gchar *top,
                          guint32 flags,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelMapStore *map_store;
	CamelMapStorePrivate *priv;
	CamelFolderInfo *fi = NULL;
	gchar *sync_state;
	gboolean initial_setup = FALSE;
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean includes_last_folder;
	gboolean success;
	GError *local_error = NULL;
	GHashTable *allfolders;
	map_store = (CamelMapStore *) store;
	priv = map_store->priv;

	//return NULL;
	g_mutex_lock (priv->get_finfo_lock);


	if (!(camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))
	      && camel_service_connect_sync ((CamelService *) store, cancellable, error))) {
		g_mutex_unlock (priv->get_finfo_lock);

		return NULL;
		//goto offline;
	}


	/* Get the folder hierarchy from the phone */
	allfolders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	create_folder_hierarchy (store, "/telecom/msg", &fi, allfolders, cancellable, error);
	g_mutex_unlock (priv->get_finfo_lock);

#if 0
	sync_state = camel_map_store_summary_get_string_val (map_store->summary, "sync_state", NULL);
	if (!sync_state)
		initial_setup = TRUE;

	if (!initial_setup && flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
		time_t now = time (NULL);

		g_free (sync_state);
		if (now - priv->last_refresh_time > FINFO_REFRESH_INTERVAL && map_refresh_finfo (map_store))
			map_store->priv->last_refresh_time = time (NULL);

		g_mutex_unlock (priv->get_finfo_lock);
		goto offline;
	}

	connection = camel_map_store_ref_connection (map_store);

	success = e_map_connection_sync_folder_hierarchy_sync (
		connection, MAP_PRIORITY_MEDIUM,
		&sync_state, &includes_last_folder,
		&folders_created, &folders_updated,
		&folders_deleted, cancellable, &local_error);

	g_object_unref (connection);

	if (!success) {
		if (local_error)
			g_warning (
				"Unable to fetch the folder hierarchy: %s :%d \n",
				local_error->message, local_error->code);
		else
			g_warning ("Unable to fetch the folder hierarchy.\n");

		camel_map_store_maybe_disconnect (map_store, local_error);
		g_propagate_error (error, local_error);

		g_mutex_unlock (priv->get_finfo_lock);
		return NULL;
	}
	map_update_folder_hierarchy (
		map_store, sync_state, includes_last_folder,
		folders_created, folders_deleted, folders_updated);
	g_mutex_unlock (priv->get_finfo_lock);

offline:
	fi = folder_info_from_store_summary ( (CamelMapStore *) store, top, flags, error);
#endif
	return fi;
}

static CamelFolderInfo *
map_create_folder_sync (CamelStore *store,
                        const gchar *parent_name,
                        const gchar *folder_name,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelMapStore *map_store = CAMEL_MAP_STORE (store);
	CamelMapStoreSummary *map_summary = map_store->summary;
	gchar *fid = NULL;
	gchar *full_name;
	CamelFolderInfo *fi = NULL;
	gboolean success;
	GError *local_error = NULL;
#if 0
	if (parent_name && *parent_name)
		full_name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	else
		full_name = g_strdup (folder_name);

	fid = camel_map_store_summary_get_folder_id_from_name (map_summary, full_name);
	if (fid) {
		g_free (fid);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot create folder '%s', folder already exists"),
			full_name);
		g_free (full_name);
		return NULL;
	}

	g_free (full_name);

	/* Get Parent folder ID */
	if (parent_name && parent_name[0]) {
		fid = camel_map_store_summary_get_folder_id_from_name (
			map_summary, parent_name);
		if (!fid) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Parent folder %s does not exist"),
				parent_name);
			return NULL;
		}

		if (g_str_equal (fid, MAP_FOREIGN_FOLDER_ROOT_ID)) {
			g_free (fid);

			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Cannot create folder under '%s', it is used for folders of other users only"),
				parent_name);
			return NULL;
		}
	}

	if (!camel_map_store_connected (map_store, error)) {
		if (fid) g_free (fid);
		return NULL;
	}

	connection = camel_map_store_ref_connection (map_store);

	success = e_map_connection_create_folder_sync (
		connection,
		MAP_PRIORITY_MEDIUM, fid,
		FALSE, folder_name, E_MAP_FOLDER_TYPE_MAILBOX,
		&folder_id, cancellable, &local_error);

	g_object_unref (connection);

	if (!success) {
		camel_map_store_maybe_disconnect (map_store, local_error);
		g_propagate_error (error, local_error);
		g_free (fid);
		return NULL;
	}

	/* Translate & store returned folder id */
	if (fid)
		full_name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	else
		full_name = g_strdup (folder_name);

	camel_map_store_summary_new_folder (
		map_summary, folder_id->id,
		fid, folder_id->change_key,
		folder_name,
		E_MAP_FOLDER_TYPE_MAILBOX,
		0, 0, FALSE);
	fi = camel_map_utils_build_folder_info (map_store, folder_id->id);
	e_map_folder_id_free (folder_id);

	camel_store_folder_created (store, fi);
	camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (map_store), fi);

	g_free (full_name);
	g_free (fid);
#endif
	return fi;
}

static gboolean
map_delete_folder_sync (CamelStore *store,
                        const gchar *folder_name,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelMapStore *map_store = CAMEL_MAP_STORE (store);
	CamelMapStoreSummary *map_summary = map_store->summary;
	gchar *fid;
	CamelFolderInfo *fi = NULL;
	gboolean success;
	GError *local_error = NULL;
#if 0
	fid = camel_map_store_summary_get_folder_id_from_name (
		map_summary, folder_name);
	if (!fid) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Folder does not exist"));
		return FALSE;
	}

	if (g_str_equal (fid, MAP_FOREIGN_FOLDER_ROOT_ID)) {
		g_free (fid);

		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot remove folder '%s', it is used for folders of other users only"),
			folder_name);
		return FALSE;
	}

	if (!camel_map_store_connected (map_store, error)) {
		g_free (fid);
		return FALSE;
	}

	if (camel_map_store_summary_get_foreign (map_store->summary, fid, NULL)) {
		/* do not delete foreign folders,
		 * just remove them from local store */
		success = TRUE;
	} else {
		EMapConnection *connection;

		connection = camel_map_store_ref_connection (map_store);

		success = e_map_connection_delete_folder_sync (
			connection,
			MAP_PRIORITY_MEDIUM,
			fid, FALSE, "HardDelete",
			cancellable, &local_error);

		g_object_unref (connection);
	}

	if (!success) {
		camel_map_store_maybe_disconnect (map_store, local_error);
		g_propagate_error (error, local_error);
		g_free (fid);
		return FALSE;
	}

	fi = camel_map_utils_build_folder_info (map_store, fid);
	camel_map_store_summary_remove_folder (map_summary, fid, error);

	camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (map_store), fi);
	camel_store_folder_deleted (store, fi);
	camel_folder_info_free (fi);

	g_free (fid);

	camel_map_store_summary_save (map_store->summary, NULL);
#endif
	return TRUE;
}



static gboolean
map_rename_folder_sync (CamelStore *store,
                        const gchar *old_name,
                        const gchar *new_name,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelMapStore *map_store = CAMEL_MAP_STORE (store);
	CamelMapStoreSummary *map_summary = map_store->summary;
	const gchar *old_slash, *new_slash;
	gchar *fid;
	gchar *changekey;
	gboolean res = FALSE;
	GError *local_error = NULL;
#if 0
	if (!strcmp (old_name, new_name))
		return TRUE;

	if (!camel_map_store_connected (map_store, error)) {
		return FALSE;
	}

	fid = camel_map_store_summary_get_folder_id_from_name (map_summary, old_name);
	if (!fid) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Folder %s does not exist"), old_name);
		return FALSE;
	}

	changekey = camel_map_store_summary_get_change_key (map_summary, fid, error);
	if (!changekey) {
		g_free (fid);
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No change key record for folder %s"), fid);
		return FALSE;
	}

	connection = camel_map_store_ref_connection (map_store);

	old_slash = g_strrstr (old_name, "/");
	new_slash = g_strrstr (new_name, "/");

	if (old_slash)
		old_slash++;
	else
		old_slash = old_name;

	if (new_slash)
		new_slash++;
	else
		new_slash = new_name;

	if (strcmp (old_slash, new_slash)) {
		gint parent_len = old_slash - old_name;
		struct _rename_cb_data *rename_data;

		/* Folder basename changed (i.e. UpdateFolder needed).
		 * Therefore, we can only do it if the folder hasn't also
		 * been moved from one parent folder to another.
 *
		 * Strictly speaking, we could probably handle this, even
		 * if there are name collisions. We could UpdateFolder to
		 * a new temporary name that doesn't exist in either the
		 * old or new parent folders, then MoveFolder, then issue
		 * another UpdateFolder to the name we actually wanted.
		 * But since the Evolution UI doesn't seem to let us
		 * make both changes at the same time anyway, we'll just
		 * bail out for now; we can deal with it later if we need
		 * to.
		*/
		if (new_slash - new_name != parent_len ||
		    strncmp (old_name, new_name, parent_len)) {
			g_set_error (
				error, CAMEL_STORE_ERROR,
				CAMEL_STORE_ERROR_INVALID,
				_("Cannot both rename and move a folder at the same time"));
			g_free (changekey);
			goto out;
		}

		rename_data = g_new0 (struct _rename_cb_data, 1);
		rename_data->display_name = new_slash;
		rename_data->folder_id = fid;
		rename_data->change_key = changekey;

		res = e_map_connection_update_folder_sync (
			connection, MAP_PRIORITY_MEDIUM,
			rename_folder_cb, rename_data,
			cancellable, &local_error);

		if (!res) {
			g_free (rename_data);
			goto out;
		}
		g_free (rename_data);
		camel_map_store_summary_set_folder_name (map_summary, fid, new_slash);
	} else {
		gchar *pfid = NULL;
		gchar *parent_name;

		/* If we are not moving to the root folder, work out the ItemId of
		 * the new parent folder */
		if (new_slash != new_name) {
			parent_name = g_strndup (new_name, new_slash - new_name - 1);
			pfid = camel_map_store_summary_get_folder_id_from_name (
				map_summary, parent_name);
			g_free (parent_name);
			if (!pfid) {
				g_set_error (
					error, CAMEL_STORE_ERROR,
					CAMEL_STORE_ERROR_NO_FOLDER,
					_("Cannot find folder ID for parent folder %s"),
					parent_name);
				goto out;
			}
		}

		res = e_map_connection_move_folder_sync (
			connection, MAP_PRIORITY_MEDIUM,
			pfid, fid, cancellable, &local_error);

		if (!res) {
			g_free (pfid);
			goto out;
		}
		camel_map_store_summary_set_parent_folder_id (map_summary, fid, pfid);
		g_free (pfid);
	}

	res = TRUE;
 out:
	if (local_error) {
		camel_map_store_maybe_disconnect (map_store, local_error);
		g_propagate_error (error, local_error);
	}

	g_object_unref (connection);

	g_free (changekey);
	g_free (fid);
#endif	
	return res;
}

gchar *
map_get_name (CamelService *service,
              gboolean brief)
{
	CamelSettings *settings;
	gchar *name;
	gchar *host;
	gchar *user;

	settings = camel_service_ref_settings (service);

	user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (settings));
	host = camel_map_settings_get_device_name ((CamelMapSettings *)settings);

	g_object_unref (settings);

	if (brief)
		name = g_strdup_printf (
			_("Messages on %s"), host);
	else
		name = g_strdup_printf (
			_("Messages for %s on %s"), user, host);

	g_free (user);

	return name;
}


static CamelFolder *
map_get_trash_folder_sync (CamelStore *store,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelMapStore *map_store;
	CamelFolder *folder = NULL;
	gchar *folder_id, *folder_name;
#if 0
	g_return_val_if_fail (CAMEL_IS_MAP_STORE (store), NULL);

	map_store = CAMEL_MAP_STORE (store);
	folder_id = camel_map_store_summary_get_folder_id_from_folder_type (
		map_store->summary, CAMEL_FOLDER_TYPE_TRASH);

	if (folder_id == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Could not locate Trash folder"));
		return NULL;
	}

	folder_name = camel_map_store_summary_get_folder_full_name (
		map_store->summary, folder_id, NULL);

	folder = map_get_folder_sync (
		store, folder_name, 0, cancellable, error);

	g_free (folder_name);
	g_free (folder_id);
#endif
	return folder;
}

static CamelFolder *
map_get_junk_folder_sync (CamelStore *store,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelMapStore *map_store;
	CamelFolder *folder = NULL;
	gchar *folder_id, *folder_name;
#if 0
	g_return_val_if_fail (CAMEL_IS_MAP_STORE (store), NULL);

	map_store = CAMEL_MAP_STORE (store);
	folder_id = camel_map_store_summary_get_folder_id_from_folder_type (
		map_store->summary, CAMEL_FOLDER_TYPE_JUNK);

	if (folder_id == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Could not locate Junk folder"));
		return NULL;
	}

	folder_name = camel_map_store_summary_get_folder_full_name (
		map_store->summary, folder_id, NULL);

	folder = map_get_folder_sync (
		store, folder_name, 0, cancellable, error);

	g_free (folder_name);
	g_free (folder_id);
#endif
	return folder;
}

static gboolean
map_can_refresh_folder (CamelStore *store,
                        CamelFolderInfo *info,
                        GError **error)
{
	CamelSettings *settings;
	CamelMapSettings *map_settings;
	gboolean check_all;

	/* Skip unselectable folders from automatic refresh */
	if (info && (info->flags & CAMEL_FOLDER_NOSELECT) != 0)
		return FALSE;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	map_settings = CAMEL_MAP_SETTINGS (settings);
	check_all = camel_map_settings_get_check_all (map_settings);

	g_object_unref (settings);

	if (check_all)
		return TRUE;

	/* Delegate decision to parent class */
	return CAMEL_STORE_CLASS (camel_map_store_parent_class)->
		can_refresh_folder (store, info, error);
}

static gboolean
map_store_folder_is_subscribed (CamelSubscribable *subscribable,
                                const gchar *folder_name)
{
	CamelMapStore *map_store = CAMEL_MAP_STORE (subscribable);
	gboolean truth = TRUE;


	return truth;
}

static gboolean
map_store_subscribe_folder_sync (CamelSubscribable *subscribable,
                                 const gchar *folder_name,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelMapStore *map_store = CAMEL_MAP_STORE (subscribable);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (map_store))) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Cannot subscribe MAP folders in offline mode"));
		return FALSE;
	}

	/* it does nothing currently */

	return TRUE;
}

static gboolean
map_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
                                   const gchar *folder_name,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelMapStore *map_store = CAMEL_MAP_STORE (subscribable);
	gboolean res = TRUE;
	gchar *fid;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (map_store))) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Cannot unsubscribe MAP folders in offline mode"));
		return FALSE;
	}

	return res;
}

static void
map_store_dispose (GObject *object)
{
	CamelMapStore *map_store;

	map_store = CAMEL_MAP_STORE (object);

	if (map_store->summary != NULL) {
		camel_map_store_summary_save (map_store->summary, NULL);
		g_object_unref (map_store->summary);
		map_store->summary = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_map_store_parent_class)->dispose (object);
}

static void
map_store_finalize (GObject *object)
{
	CamelMapStore *map_store;

	map_store = CAMEL_MAP_STORE (object);

	g_free (map_store->storage_path);
	g_mutex_free (map_store->priv->get_finfo_lock);
	g_mutex_free (map_store->priv->connection_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_map_store_parent_class)->finalize (object);
}

static void
camel_map_store_class_init (CamelMapStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	g_type_class_add_private (class, sizeof (CamelMapStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = map_store_dispose;
	object_class->finalize = map_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_MAP_SETTINGS;
	service_class->query_auth_types_sync = map_store_query_auth_types_sync;
	service_class->get_name = map_get_name;
	service_class->connect_sync = map_connect_sync;
	service_class->disconnect_sync = map_disconnect_sync;
	service_class->authenticate_sync = map_authenticate_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder_sync = map_get_folder_sync;
	store_class->create_folder_sync = map_create_folder_sync;
	store_class->delete_folder_sync = map_delete_folder_sync;
	store_class->rename_folder_sync = map_rename_folder_sync;
	store_class->get_folder_info_sync = map_get_folder_info_sync;
	store_class->free_folder_info = camel_store_free_folder_info_full;

	store_class->get_trash_folder_sync = map_get_trash_folder_sync;
	store_class->get_junk_folder_sync = map_get_junk_folder_sync;
	store_class->can_refresh_folder = map_can_refresh_folder;
}

static void
camel_map_subscribable_init (CamelSubscribableInterface *interface)
{
	interface->folder_is_subscribed = map_store_folder_is_subscribed;
	interface->subscribe_folder_sync = map_store_subscribe_folder_sync;
	interface->unsubscribe_folder_sync = map_store_unsubscribe_folder_sync;
}

static void
camel_map_store_init (CamelMapStore *map_store)
{
	map_store->priv =
		CAMEL_MAP_STORE_GET_PRIVATE (map_store);

	map_store->priv->connection = NULL;
	map_store->priv->last_refresh_time = time (NULL) - (FINFO_REFRESH_INTERVAL + 10);
	map_store->priv->get_finfo_lock = g_mutex_new ();
	map_store->priv->connection_lock = g_mutex_new ();
}
