/*
 * camel-map-settings.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef CAMEL_MAP_SETTINGS_H
#define CAMEL_MAP_SETTINGS_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MAP_SETTINGS \
	(camel_map_settings_get_type ())
#define CAMEL_MAP_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAP_SETTINGS, CamelMapSettings))
#define CAMEL_MAP_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAP_SETTINGS, CamelMapSettingsClass))
#define CAMEL_IS_MAP_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAP_SETTINGS))
#define CAMEL_IS_MAP_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAP_SETTINGS))
#define CAMEL_MAP_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAP_SETTINGS))

G_BEGIN_DECLS

typedef struct _CamelMapSettings CamelMapSettings;
typedef struct _CamelMapSettingsClass CamelMapSettingsClass;
typedef struct _CamelMapSettingsPrivate CamelMapSettingsPrivate;

struct _CamelMapSettings {
	CamelOfflineSettings parent;
	CamelMapSettingsPrivate *priv;
};

struct _CamelMapSettingsClass {
	CamelOfflineSettingsClass parent_class;
};

GType		camel_map_settings_get_type	(void) G_GNUC_CONST;
gboolean	camel_map_settings_get_check_all 
						(CamelMapSettings *settings);
void		camel_map_settings_set_check_all 
						(CamelMapSettings *settings,
						 gboolean check_all);
const gchar *	camel_map_settings_get_device_name
						(CamelMapSettings *settings);
void		camel_map_settings_set_device_name
						(CamelMapSettings *settings,
						 const gchar *device);
const gchar *	camel_map_settings_get_device_str_address
						(CamelMapSettings *settings);
void		camel_map_settings_set_device_str_address
						(CamelMapSettings *settings,
						 const gchar *device);
const gchar *	camel_map_settings_get_email 	(CamelMapSettings *settings);
void		camel_map_settings_set_email 	(CamelMapSettings *settings,
                              			 const gchar *email);
gboolean	camel_map_settings_get_filter_junk 
						(CamelMapSettings *settings);
void		camel_map_settings_set_filter_junk 
						(CamelMapSettings *settings,
                                    		 gboolean filter_junk);
gboolean	camel_map_settings_get_filter_junk_inbox 
						(CamelMapSettings *settings);
void		camel_map_settings_set_filter_junk_inbox 
						(CamelMapSettings *settings,
                                          	 gboolean filter_junk_inbox);


const gchar *	camel_map_settings_get_service_name
						(CamelMapSettings *settings);
void		camel_map_settings_set_service_name
						(CamelMapSettings *settings,
						 const gchar *name);
guint		camel_map_settings_get_channel	(CamelMapSettings *settings);
void		camel_map_settings_set_channel	(CamelMapSettings *settings,
						 guint channel);
G_END_DECLS

#endif /* CAMEL_MAP_SETTINGS_H */
