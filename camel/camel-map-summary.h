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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_MAP_SUMMARY_H
#define CAMEL_MAP_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MAP_SUMMARY \
	(camel_map_summary_get_type ())
#define CAMEL_MAP_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAP_SUMMARY, CamelMapSummary))
#define CAMEL_MAP_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAP_SUMMARY, CamelMapSummaryClass))
#define CAMEL_IS_MAP_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAP_SUMMARY))
#define CAMEL_IS_MAP_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAP_SUMMARY))
#define CAMEL_MAP_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAP_SUMMARY, CamelMapSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelMapSummary CamelMapSummary;
typedef struct _CamelMapSummaryClass CamelMapSummaryClass;
typedef struct _CamelMapMessageInfo CamelMapMessageInfo;
typedef struct _CamelMapMessageContentInfo CamelMapMessageContentInfo;


struct _CamelMapMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
} ;

struct _CamelMapMessageContentInfo {
	CamelMessageContentInfo info;
} ;

struct _CamelMapSummary {
	CamelFolderSummary parent;

	gint32 version;
} ;

struct _CamelMapSummaryClass {
	CamelFolderSummaryClass parent_class;
} ;

GType camel_map_summary_get_type (void);

CamelFolderSummary *
	camel_map_summary_new		(struct _CamelFolder *folder);
gboolean
	camel_map_update_message_info_flags
					(CamelFolderSummary *summary,
					 CamelMessageInfo *info,
					 guint32 server_flags,
					 CamelFlag *server_user_flags);
void	camel_map_summary_add_message	(CamelFolderSummary *summary,
					 const gchar *uid,
					 CamelMimeMessage *message);
void	map_summary_clear		(CamelFolderSummary *summary,
					 gboolean uncache);

G_END_DECLS

#endif /* CAMEL_MAP_SUMMARY_H */
