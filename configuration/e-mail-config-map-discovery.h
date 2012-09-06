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


#ifndef E_MAIL_CONFIG_MAP_DISCOVERY_H
#define E_MAIL_CONFIG_MAP_DISCOVERY_H

#include <glib.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <netinet/in.h>
#include <string.h>

typedef struct _MapProvider {
	char *device_name;
	char str_address[20];
	bdaddr_t baddress;
	char *service_name;
	guint8 channel;
}MapProvider;

int	e_mail_config_map_discover_service 	(GList **);
void	e_mail_config_map_free_provider 	(MapProvider *provider);
MapProvider *
	e_mail_config_map_provider_from_text 
						(const char *device_name,
						 const char *str_address,
						 const char *service_name,
						 guint channel);
					
#endif
