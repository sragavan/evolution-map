/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-map-store.h : class for an map store */

/*
 * Authors: Srinivasa Ragavan <sragavan@gnome.org>
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

#ifndef CAMEL_MAP_STORE_H
#define CAMEL_MAP_STORE_H

#include <camel/camel.h>

#include "camel-map-store-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_MAP_STORE \
	(camel_map_store_get_type ())
#define CAMEL_MAP_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAP_STORE, CamelMapStore))
#define CAMEL_MAP_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAP_STORE, CamelMapStoreClass))
#define CAMEL_IS_MAP_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAP_STORE))
#define CAMEL_IS_MAP_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAP_STORE))
#define CAMEL_MAP_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAP_STORE, CamelMapStoreClass))

#define MAP_PARAM_FILTER_INBOX		(1 << 0)

#define MAP_FOREIGN_FOLDER_ROOT_ID		"ForeignRoot"
#define MAP_FOREIGN_FOLDER_ROOT_DISPLAY_NAME	_("Foreign Folders")

G_BEGIN_DECLS

typedef struct _CamelMapStore CamelMapStore;
typedef struct _CamelMapStoreClass CamelMapStoreClass;
typedef struct _CamelMapStorePrivate CamelMapStorePrivate;

struct _CamelMapStore {
	CamelOfflineStore parent;
	CamelMapStorePrivate *priv;

	CamelMapStoreSummary *summary;
	gchar *storage_path;
};

struct _CamelMapStoreClass {
	CamelOfflineStoreClass parent_class;
};

GType camel_map_store_get_type (void);

G_END_DECLS

#endif /* CAMEL_MAP_STORE_H */
