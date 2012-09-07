
#ifndef CAMEL_MAP_DBUS_UTILS_H
#define CAMEL_MAP_DBUS_UTILS_H

#include <gio/gio.h>
#include <glib.h>

GDBusConnection *	camel_map_connect_dbus 			(GCancellable *cancellable,
                  						 GError **error);
char *			camel_map_connect_device_channel 	(GDBusConnection *connection, 
								 char *device, 
								 guint channel, 
								 GCancellable *cancellable,
								 GError **error);
GVariant *		camel_map_dbus_set_current_folder 	(GDBusProxy *object,
								 const char *folder,
							         GCancellable *cancellable,
								 GError **error);
GVariant *		camel_map_dbus_get_folder_listing 	(GDBusProxy *object,
							         GCancellable *cancellable,
								 GError **error);		



#endif
