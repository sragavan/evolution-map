
#ifndef CAMEL_MAP_DBUS_UTILS_H
#define CAMEL_MAP_DBUS_UTILS_H

#include <gio/gio.h>
#include <glib.h>

GDBusConnection *	camel_map_connect_dbus 			(GCancellable *cancellable,
                  						 GError **error);
char *			camel_map_connect_device_channel 	(GDBusConnection *connection, 
								 const char *device, 
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
GVariant *		camel_map_dbus_get_message_listing 	(GDBusProxy *object,
								 const char *folder_path,
								 GCancellable *cancellable,
								 GError **error);
gboolean		camel_map_dbus_get_message 		(GDBusProxy *object,
								 const char *message_object_id,
								 const char *file_name,
								 GCancellable *cancellable,
								 GError **error);

gboolean		camel_map_dbus_set_message_read 	(GDBusProxy *object,
								 const char *msg_id,
								 gboolean read,
								 GCancellable *cancellable,
								 GError **error);
gboolean		camel_map_dbus_set_message_deleted 	(GDBusProxy *object,
								 const char *msg_id,
								 gboolean read,
								 GCancellable *cancellable,
								 GError **error);

gboolean		camel_map_dbus_update_inbox 		(GDBusProxy *proxy,
                             					 GCancellable *cancellable,
                             					 GError **error);

gboolean		camel_map_dbus_set_notification_registration
								(GDBusProxy *proxy,
								 gboolean reg,
								 GCancellable *cancellable,
								 GError **error);
#endif
