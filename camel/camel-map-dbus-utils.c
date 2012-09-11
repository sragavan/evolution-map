
#include <stdio.h>
#include <glib.h>
#include <gio/gio.h>

#include "camel-map-dbus-utils.h"

GDBusConnection *
camel_map_connect_dbus (GCancellable *cancellable,
                  	GError **error)
{
	GDBusConnection *connection;

	connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

	return connection;
}

char *
camel_map_connect_device_channel (GDBusConnection *connection, 
				  char *device, 
				  guint channel, 
				  GCancellable *cancellable,
				  GError **error)
{
	GDBusMessage *m;
	GDBusMessage *r;
	GVariant *v;
	GVariantBuilder *b;
	GVariant *dict;
	char *str_channel;
	char *path=NULL;

	str_channel = g_strdup_printf("%u", channel);

	m = g_dbus_message_new_method_call ("org.bluez.obex.client", /* name */
                                      "/", /* path */
                                      "org.bluez.obex.Client", /* interface */
                                      "CreateSession");


	b = g_variant_builder_new (G_VARIANT_TYPE ("(sa{sv})"));
	g_variant_builder_add (b, "s", device);
	g_variant_builder_open (b, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (b, "{sv}", "Target", g_variant_new_string("map"));
	g_variant_builder_add (b, "{sv}", "Channel", g_variant_new_string (str_channel));
	g_variant_builder_close (b);
	dict = g_variant_builder_end (b);
	
	g_dbus_message_set_body (m, dict);
	
	r = g_dbus_connection_send_message_with_reply_sync (connection, m, G_DBUS_SEND_MESSAGE_FLAGS_NONE, 
			-1, NULL, cancellable, error);		
	g_free (str_channel);
	g_variant_unref(dict);
	g_variant_builder_unref(b);

	if (r == NULL)
		return NULL;

	v = g_dbus_message_get_body (r);
	g_variant_get (v, "(o)", &path);

	printf("Path: %s\n", path);

	return path;
}

GVariant *
camel_map_dbus_set_current_folder (GDBusProxy *object,
				   const char *folder,
				   GCancellable *cancellable,
				   GError **error)
{
	GVariant *ret;

	ret = g_dbus_proxy_call_sync (object,
			"SetFolder",
			g_variant_new ("(s)", folder),
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			cancellable,
			error);

	return ret;
}

GVariant *
camel_map_dbus_get_folder_listing (GDBusProxy *object,
				   GCancellable *cancellable,
				   GError **error)
{
	GVariant *ret, *v;
	GVariantBuilder *b;

	b = g_variant_builder_new (G_VARIANT_TYPE ("(a{ss})"));
	g_variant_builder_open (b, G_VARIANT_TYPE ("a{ss}"));	
	g_variant_builder_close (b);	
	v = g_variant_builder_end (b);
	
	ret = g_dbus_proxy_call_sync (object,
			"GetFolderListing",
			v,
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			cancellable,
			error);

	g_variant_builder_unref(b);

	return ret;
}
