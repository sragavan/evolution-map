/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-map-folder.h: class for an map folder */

/*
 * Authors:
 *   Srinivasa Ragavan <sragavan@gnome.org>
 *
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

#ifndef CAMEL_MAP_FOLDER_H
#define CAMEL_MAP_FOLDER_H

#include <camel/camel.h>
#include <gio/gio.h>
#include <glib.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MAP_FOLDER \
	(camel_map_folder_get_type ())
#define CAMEL_MAP_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAP_FOLDER, CamelMapFolder))
#define CAMEL_MAP_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAP_FOLDER, CamelMapFolderClass))
#define CAMEL_IS_MAP_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAP_FOLDER))
#define CAMEL_IS_MAP_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAP_FOLDER))
#define CAMEL_MAP_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAP_FOLDER, CamelMapFolderClass))

G_BEGIN_DECLS

typedef struct _CamelMapFolder CamelMapFolder;
typedef struct _CamelMapFolderClass CamelMapFolderClass;
typedef struct _CamelMapFolderPrivate CamelMapFolderPrivate;

struct _CamelMapFolder {
	CamelOfflineFolder parent;
	CamelMapFolderPrivate *priv;

	CamelFolderSearch *search;
	CamelDataCache *cache;
};

struct _CamelMapFolderClass {
	CamelOfflineFolderClass parent_class;
};

GType camel_map_folder_get_type (void);

/* implemented */
CamelFolder * 			camel_map_folder_new 	(CamelStore *store,
							 GDBusProxy *map,
							 const gchar *map_dir,
							 const gchar *folder_dir,
							 const gchar *folder_name,
							 GCancellable *cancellable,
							 GError **error);
void				camel_map_folder_mark_message_read 
							(CamelMapFolder *map_folder,
							 const char *uid,
							 gboolean read);
void				camel_map_folder_mark_message_deleted
							(CamelMapFolder *map_folder,
							 const char *uid,
							 gboolean read);


G_END_DECLS

#endif /* CAMEL_MAP_FOLDER_H */
