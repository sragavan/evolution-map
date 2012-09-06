/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *   Srinivasa Ragavan <sragavan@gnome.org>
 *
 * Copyright (C) 2012 Intel Corporation. (www.intel.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <camel/camel.h>
#include <glib/gi18n-lib.h>

#include "camel-map-store.h"

static guint map_url_hash (gconstpointer key);
static gint  map_url_equal (gconstpointer a, gconstpointer b);

CamelProviderConfEntry map_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for New Mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check-all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "folders", NULL,
	  N_("Folders") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
//	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-all", NULL,
//	  N_("Apply _filters to new messages in all folders"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-inbox", "!filter-all",
	  N_("_Apply filters to new messages in Inbox on this server"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "stay-synchronized", NULL,
	  N_("Automatically synchroni_ze remote mail locally"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider map_provider = {
	"map",

	N_("MAP"),

	N_("For reading and storing mail on mobile phones via Bluetooth MAP profile."),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_SUPPORTS_SSL|
	CAMEL_PROVIDER_SUPPORTS_MOBILE_DEVICES |
	CAMEL_PROVIDER_SUPPORTS_BATCH_FETCH |
	CAMEL_PROVIDER_SUPPORTS_PURGE_MESSAGE_CACHE,

	0, //CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH,

	map_conf_entries,

	NULL,

	/* ... */
};

//extern CamelServiceAuthType camel_map_password_authtype;

void camel_map_module_init (void);

void
camel_map_module_init (void)
{
	map_provider.object_types[CAMEL_PROVIDER_STORE] = camel_map_store_get_type ();
	map_provider.url_hash = map_url_hash;
	map_provider.url_equal = map_url_equal;
//	map_provider.authtypes = camel_sasl_authtype_list (FALSE);
	map_provider.translation_domain = GETTEXT_PACKAGE;

	camel_provider_register (&map_provider);
}

void
camel_provider_module_init (void)
{
	camel_map_module_init ();
}

static void
map_add_hash (guint *hash,
                gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
map_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *) key;
	guint hash = 0;

	map_add_hash (&hash, u->user);
	map_add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
map_check_equal (gchar *s1,
                   gchar *s2)
{
	if (s1 == NULL) {
		if (s2 == NULL)
			return TRUE;
		else
			return FALSE;
	}

	if (s2 == NULL)
		return FALSE;

	return strcmp (s1, s2) == 0;
}

static gint
map_url_equal (gconstpointer a,
                 gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return map_check_equal (u1->protocol, u2->protocol)
		&& map_check_equal (u1->user, u2->user)
		&& map_check_equal (u1->host, u2->host)
		&& u1->port == u2->port;
}
