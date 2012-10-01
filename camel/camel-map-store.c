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
#include "camel-map-folder.h"
#include "camel-map-summary.h"
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

#define CURRENT_FOLDER_LOCK() g_rec_mutex_lock(&map_store->priv->current_folder_lock);
#define CURRENT_FOLDER_UNLOCK() g_rec_mutex_unlock(&map_store->priv->current_folder_lock);
#define CURRENT_FOLDER(folder) g_free(map_store->priv->current_selected_folder); map_store->priv->current_selected_folder = g_strdup(folder); printf("Setting current folder to %s\n", folder);

struct _CamelMapStorePrivate {
	char *session_path;
	GDBusProxy *session;
	GDBusProxy *map;
	GDBusConnection *connection;	
	time_t last_refresh_time;
	GMutex *get_finfo_lock;
	GMutex *connection_lock;
	GRecMutex current_folder_lock;
	char *current_selected_folder;
	gboolean initial_fetch;
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
	gboolean success = TRUE;
	CamelSettings *settings;
	CamelMapSettings *map_settings;
	GVariant *ret;
	
	map_store = CAMEL_MAP_STORE (service);
	
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


	CURRENT_FOLDER_LOCK();
	ret = camel_map_dbus_set_current_folder (map_store->priv->map,
						 "/telecom/msg",
						 cancellable,
						 error);
	if (ret == NULL) {
		printf("Set folder to telecom/msg failed\n");
		CURRENT_FOLDER_UNLOCK();
		return FALSE;
	}
	CURRENT_FOLDER("/telecom/msg");
	printf("SETFOLDER: %s\n", g_variant_print (ret, TRUE));

	ret = camel_map_dbus_get_folder_listing (map_store->priv->map,
						 cancellable,
						 error);

	CURRENT_FOLDER_UNLOCK();
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

	g_mutex_lock (map_store->priv->connection_lock);
	g_object_unref (map_store->priv->session);
	map_store->priv->session = NULL;
	g_object_unref (map_store->priv->map);
	map_store->priv->map = NULL;
	g_object_unref (map_store->priv->connection);
	map_store->priv->connection = NULL;
	g_free (map_store->priv->session_path);
	map_store->priv->session_path = NULL;

	g_mutex_unlock (map_store->priv->connection_lock);

	service_class = CAMEL_SERVICE_CLASS (camel_map_store_parent_class);
	return service_class->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
map_authenticate_sync (CamelService *service,
                       const gchar *mechanism,
                       GCancellable *cancellable,
                       GError **error)
{
	return CAMEL_AUTHENTICATION_ACCEPTED;
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
	gchar *fid, *folder_dir, *map_dir;
	GVariant *ret;

	map_dir = g_strdup_printf ("/telecom/msg/%s", folder_name);
	map_store = (CamelMapStore *)store;
	CURRENT_FOLDER_LOCK();
	ret = camel_map_dbus_set_current_folder (map_store->priv->map,
						 map_dir,
						 cancellable,
						 error);
	CURRENT_FOLDER_UNLOCK();
	if (!ret) {
		printf("Unable to set current folder to: %s\n", map_dir);
		return NULL;
	}
	folder_dir = g_build_filename (map_store->storage_path, "folders", folder_name, NULL);
	folder = camel_map_folder_new (store, map_store->priv->map, map_dir, folder_name, folder_dir, cancellable, error);

	g_free (folder_dir);

	return folder;
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
	CURRENT_FOLDER(parent);
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
				folder = (gchar *)g_variant_get_string (value, NULL);

				if (!folder || !*folder || strcmp (folder, "msg") == 0 || strcmp (folder, "MSG") == 0)
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
	GHashTable *allfolders;
	map_store = (CamelMapStore *) store;
	priv = map_store->priv;

	g_mutex_lock (priv->get_finfo_lock);

	if (!(camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))
	      && camel_service_connect_sync ((CamelService *) store, cancellable, error))) {
		g_mutex_unlock (priv->get_finfo_lock);

		return NULL;
	}


	/* Get the folder hierarchy from the phone */
	allfolders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	CURRENT_FOLDER_LOCK()
	create_folder_hierarchy (store, "/telecom/msg", &fi, allfolders, cancellable, error);
	CURRENT_FOLDER_UNLOCK();
	g_mutex_unlock (priv->get_finfo_lock);

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

	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Cannot create folder '%s'. MAP doesn't support"),
		folder_name);

	return NULL;
}

static gboolean
map_delete_folder_sync (CamelStore *store,
                        const gchar *folder_name,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelMapStore *map_store = CAMEL_MAP_STORE (store);
	CamelMapStoreSummary *map_summary = map_store->summary;

	/* MAP doesn't support delete folder */
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Cannot delete folder '%s'. MAP doesn't support"),
		folder_name);
	return FALSE;
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
	gboolean res = FALSE;
	
	/* MAP doesn't support rename */
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Cannot rename folder '%s'. MAP doesn't support"),
		old_name);

	return res;
}

static gchar *
map_get_name (CamelService *service,
              gboolean brief)
{
	CamelSettings *settings;
	gchar *name;
	const gchar *host;
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
	g_rec_mutex_clear (&map_store->priv->current_folder_lock);

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

	map_store->priv->initial_fetch = TRUE;
	map_store->priv->connection = NULL;
	map_store->priv->last_refresh_time = time (NULL) - (FINFO_REFRESH_INTERVAL + 10);
	map_store->priv->get_finfo_lock = g_mutex_new ();
	map_store->priv->connection_lock = g_mutex_new ();
	g_rec_mutex_init(&map_store->priv->current_folder_lock);
	map_store->priv->current_selected_folder = NULL;

}

gboolean
camel_map_store_set_current_folder (CamelMapStore *map_store,
				    const char *folder,
				    GCancellable *cancellable,
				    GError **error)
{
	GVariant *ret;
	/* Its a recurring lock, so no harm locking it again. */
	CURRENT_FOLDER_LOCK();

	/* It appears that we have to forcefully set the current folder for certain ops to work,
	   even though we are in the same folder. */
        /*
	if (0 && g_strcmp0(map_store->priv->current_selected_folder, folder) == 0) {
		CURRENT_FOLDER_UNLOCK();
		return TRUE;
		}
	*/

	ret = camel_map_dbus_set_current_folder (map_store->priv->map,
						 folder,
						 cancellable,
						 error);
	if (ret == NULL) {
		printf("Set folder to %s failed\n", folder);
		CURRENT_FOLDER_UNLOCK();
		return FALSE;
	}
	CURRENT_FOLDER(folder);
	CURRENT_FOLDER_UNLOCK();

	return TRUE;
}

void
camel_map_store_folder_lock (CamelMapStore *map_store)
{
	CURRENT_FOLDER_LOCK();
}

void 
camel_map_store_folder_unlock (CamelMapStore *map_store)
{
	CURRENT_FOLDER_UNLOCK();
}

const char *
camel_map_store_get_map_session_path (CamelMapStore *map_store)
{
	return map_store->priv->session_path;
}

gboolean
camel_map_store_get_initial_fetch (CamelMapStore *map_store)
{
	return map_store->priv->initial_fetch;
}

void
camel_map_store_set_initial_fetch (CamelMapStore *map_store, gboolean fetch)
{
	map_store->priv->initial_fetch = fetch;
}
