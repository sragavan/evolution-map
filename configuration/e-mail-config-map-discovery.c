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

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <netinet/in.h>
#include <string.h>

#include <camel/camel.h>
#include <glib/gi18n-lib.h>

#include "e-mail-config-map-discovery.h"

int
e_mail_config_map_discover_service (GList **providers)
{
	GList *list=NULL;
	MapProvider *provider;
    	inquiry_info info[25];
	bdaddr_t interface;
	int dev_id, dd;
	guint8 count=0;
	int i;

	bacpy(&interface, BDADDR_ANY);
	dev_id = hci_get_route(NULL);
    	dd = hci_open_dev(dev_id);
	if (dd < 0) {
		g_warning ("HCI device open failed\n");
		return -1;
	}

	if (sdp_general_inquiry(info, 25, 8, &count) < 0) {
		printf("No devices found\n");
		return 0;
	}

	for (i=0; i< count; i++) {
		char name[248];
		guint32 range = 0x0000ffff;
		guint16 class16 = 0x1132 & 0xffff; /* 0x1132 is the Class ID for MAP profile */
		uuid_t group;
		sdp_list_t *attrid, *search, *seq, *next;
		sdp_session_t *session;
		char str[20];
	
		sdp_uuid16_create(&group, class16);
		attrid = sdp_list_append(0, &range);
		search = sdp_list_append(0, &group);

		session = sdp_connect(&interface, &info[i].bdaddr, SDP_RETRY_IF_BUSY);
		if (!session)
			return -1;
		sdp_service_search_attr_req(session, search, SDP_ATTR_REQ_RANGE, attrid, &seq);
		sdp_list_free(attrid, 0);
		sdp_list_free(search, 0);
	
		hci_read_remote_name(dd, &info[i].bdaddr, sizeof(name), name, 25000);
		printf("Name: %s\n", name);
		ba2str(&info[i].bdaddr, str);
		printf("Address: %s\n", str);
	
		for (; seq; seq = next) {
			sdp_record_t *rec = (sdp_record_t *) seq->data;
	    		sdp_data_t *d = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);
			sdp_list_t *proto = 0, *list1, *list2;
			gboolean found_channel = FALSE;
			if (d)
				printf("Service Name: %.*s\n", d->unitSize, d->val.str);
			sdp_get_access_protos(rec, &proto);
			list1 = proto;
			while (list1 && !found_channel) {
				sdp_list_t *protDescSeq = (sdp_list_t *)list1->data;

				list2 = protDescSeq;
				while (list2 && !found_channel) {
					sdp_data_t *p = (sdp_data_t *)list2->data;
					int i = 0, proto = 0;

					for (; p; p = p->next, i++) {
						switch (p->dtd) {
							case SDP_UUID16:
							case SDP_UUID32:
							case SDP_UUID128:
								proto = sdp_uuid_to_proto(&p->val.uuid);
								break;
							case SDP_UINT8:
								if (proto == RFCOMM_UUID) {
									provider = g_new0(MapProvider, 1);
									provider->device_name = g_strdup (name);
									provider->baddress = info[i].bdaddr;
									strcpy (provider->str_address, str);
									provider->service_name = g_strdup_printf("%.*s",  d->unitSize, d->val.str);
									provider->channel = p->val.uint8;
									printf("Channel: %d\n", p->val.uint8);
									found_channel = TRUE;
									list = g_list_append (list, provider);
								}
								break;
						}
					}
					list2 = list2->next;
				}
				list1 = list1->next;
			}
		
			next = seq->next;
		}
	
	}

	*providers = list;
	printf("List length = :%d\n", g_list_length(*providers));
	return 0;

}

void	
e_mail_config_map_free_provider (MapProvider *provider)
{
	g_free (provider->device_name);
	g_free (provider->service_name);
	g_free (provider);
}

MapProvider * e_mail_config_map_provider_from_text (const char *device_name,
						    const char *str_address,
						    const char *service_name,
						    guint channel)
{
	MapProvider *provider = g_new0 (MapProvider, 1);
	
	provider->device_name = g_strdup (device_name);
	strcpy (provider->str_address, str_address);
	provider->service_name = g_strdup (service_name);
	provider->channel = (guint8) channel;
	str2ba (str_address, &provider->baddress);

	return provider;
}
