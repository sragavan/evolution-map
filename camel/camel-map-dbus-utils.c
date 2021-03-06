
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
				  const char *device, 
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

	m = g_dbus_message_new_method_call ("org.bluez.obex", /* name */
                                      "/org/bluez/obex", /* path */
                                      "org.bluez.obex.Client1", /* interface */
                                      "CreateSession");


	b = g_variant_builder_new (G_VARIANT_TYPE ("(sa{sv})"));
	g_variant_builder_add (b, "s", device);
	g_variant_builder_open (b, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (b, "{sv}", "Target", g_variant_new_string("map"));
	g_variant_builder_add (b, "{sv}", "Channel", g_variant_new_byte ((guchar)channel));
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
	printf("CreateSession: %s\n", g_variant_print (v, TRUE));
	g_variant_get (v, "(o)", &path);

	printf("Path: %s\n", path);

	return path;
}


typedef struct _transfer_handler {
    GMutex lock;
    GCond cond;
    gboolean complete;
    gboolean error;
    GVariant *result;
}TransferHandler;

static void 
transfer_on_signal (GDBusProxy *proxy,
		    GVariant   *changed_properties,
		    GStrv       invalidated_properties,
		    gpointer    user_data)
{
    TransferHandler *handler = (TransferHandler *)user_data;
    GVariantIter iter;
    GVariant *value;
    gchar *key;
    guint i;


    printf("%s\n", g_variant_print (changed_properties, TRUE));
    g_variant_iter_init (&iter, changed_properties);
    
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
		if (g_ascii_strcasecmp (key, "Status") == 0) {
			const char *status = g_variant_get_string (value, NULL);

			if (g_ascii_strcasecmp (status, "error") == 0) {
				handler->complete = TRUE;
				handler->error = TRUE;
				printf("Launching cond signal handler after error\n");
				g_cond_signal (&handler->cond);
			} else if (g_ascii_strcasecmp (status, "complete") == 0) {
				handler->complete = TRUE;
				handler->error = FALSE;
				printf("Launching cond signal handler after complete\n");
				g_cond_signal (&handler->cond);
			}
		}
	    g_free (key);
	    g_variant_unref (value);
    }

}

gboolean
camel_map_dbus_get_message (GDBusProxy *object,
			    const char *message_object_id,
			    const char *file_name,
			    GCancellable *cancellable,
			    GError **error)
{
	GVariant *ret, *v;
	GVariantBuilder *b;
	GDBusProxy *message, *transfer;
	TransferHandler *handler;
	gint64 end_time;
	gboolean success = FALSE;
	GVariant *prop, *item;
	GVariantIter top_iter;
	char *transfer_obj;
	
	message = g_dbus_proxy_new_sync (g_dbus_proxy_get_connection(object),
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					"org.bluez.obex",
					message_object_id,
					"org.bluez.obex.Message1",
					cancellable,
					error);

	if (!message)
	    return FALSE;

	b = g_variant_builder_new (G_VARIANT_TYPE ("(sb)"));
	g_variant_builder_add (b, "s", file_name);
	g_variant_builder_add (b, "b", TRUE);
	v = g_variant_builder_end (b);
	
	ret = g_dbus_proxy_call_sync (message,
			"Get",
			v,
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			cancellable,
			error);
	
	
	printf("*************** %s\n", ret ? g_variant_print(ret, TRUE) :((*error)->message));
	g_object_unref (message);

	/* Get the transfer object (oa{sv}) */
	
	g_variant_iter_init (&top_iter, ret);
	//item = g_variant_iter_next_value (&top_iter);
	g_variant_get (ret, "(&o@a{sv})", &transfer_obj, &prop);
	printf("TRA: %s\n", transfer_obj);
	//g_object_unref (prop);

	/* Co-ordinate with Transfer handler and return synchronously */
	handler = g_new0 (TransferHandler, 1);
	g_cond_init (&handler->cond);
	g_mutex_init (&handler->lock);
	handler->complete = FALSE;
	handler->result = NULL;
	
	g_mutex_lock (&handler->lock);
	/* We would kinda wait 2 mins */
	end_time = g_get_monotonic_time () + 120 * G_TIME_SPAN_SECOND;
	transfer = g_dbus_proxy_new_sync (g_dbus_proxy_get_connection(object),
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					"org.bluez.obex",
					transfer_obj,
					"org.bluez.obex.Transfer1",
					cancellable,
					error);

	g_signal_connect (transfer, "g-properties-changed", G_CALLBACK (transfer_on_signal), handler);

	while (!handler->complete) {
	    printf("going to wait\n");
	    if (!g_cond_wait_until (&handler->cond, &handler->lock, end_time)) {
		// timeout has passed.
		g_mutex_unlock (&handler->lock);
		break;
	    }
	}
	printf("Awake\n");
	g_mutex_clear (&handler->lock);
	g_cond_clear (&handler->cond);
	success = handler->error != TRUE;
	g_free (handler);
	
	printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&& %s\n", transfer_obj);
	return success;

}


gboolean
camel_map_dbus_set_message_read (GDBusProxy *object,
				 const char *msg_id,
				 gboolean read,
				 GCancellable *cancellable,
				 GError **error)
{
    GVariant *ret;
    GDBusProxy *message;
    GError *lerr = NULL;
    
    message = g_dbus_proxy_new_sync (g_dbus_proxy_get_connection(object),
				     G_DBUS_PROXY_FLAGS_NONE,
				     NULL,
				     "org.bluez.obex",
				     msg_id,
				     "org.freedesktop.DBus.Properties",
				     cancellable,
				     &lerr);
    if (!message) {
	printf("ERROR: %s\n", lerr->message);
	return FALSE;
    }
    
    printf("Setprop \n");
    ret = g_dbus_proxy_call_sync (message,
				  "org.freedesktop.DBus.Properties.Set",
				  g_variant_new ("(ssv)", "org.bluez.obex.Message1", "Read", g_variant_new_boolean(read)),
				  G_DBUS_CALL_FLAGS_NONE,
				  -1,
				  cancellable,
				  error);

    return ret != NULL;
}

gboolean
camel_map_dbus_set_message_deleted (GDBusProxy *object,
				    const char *msg_id,
				    gboolean deleted,
				    GCancellable *cancellable,
				    GError **error)
{
    GVariant *ret;
    GDBusProxy *message;

    message = g_dbus_proxy_new_sync (g_dbus_proxy_get_connection(object),
				     G_DBUS_PROXY_FLAGS_NONE,
				     NULL,
				     "org.bluez.obex",
				     msg_id,
				     "org.freedesktop.DBus.Properties",
				     cancellable,
				     error);
    if (!message)
	return FALSE;
    
    ret = g_dbus_proxy_call_sync (message,
				  "org.freedesktop.DBus.Properties.Set",
				  g_variant_new ("(ssv)", "org.bluez.obex.Message1", "Deleted", g_variant_new_boolean(deleted)),
				  G_DBUS_CALL_FLAGS_NONE,
				  -1,
				  cancellable,
				  error);

    return ret != NULL;
}


GVariant *
camel_map_dbus_set_current_folder (GDBusProxy *object,
				   const char *folder,
				   GCancellable *cancellable,
				   GError **error)
{
	GVariant *ret;

	printf("Set current folder to : %s\n", folder);
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

	b = g_variant_builder_new (G_VARIANT_TYPE ("(a{sv})"));
	g_variant_builder_open (b, G_VARIANT_TYPE ("a{sv}"));	
	g_variant_builder_close (b);	
	v = g_variant_builder_end (b);
	
	ret = g_dbus_proxy_call_sync (object,
			"ListFolders",
			v,
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			cancellable,
			error);

	g_variant_builder_unref(b);

	return ret;
}

GVariant *
camel_map_dbus_get_message_listing (GDBusProxy *object,
				    const char *folder_full_name,
				    GCancellable *cancellable,
				    GError **error)
{
	GVariant *ret, *v;
	GVariantBuilder *b;

	b = g_variant_builder_new (G_VARIANT_TYPE ("(sa{sv})"));
	g_variant_builder_add  (b, "s", folder_full_name);
	g_variant_builder_open (b, G_VARIANT_TYPE ("a{sv}"));	
	g_variant_builder_close (b);	
	v = g_variant_builder_end (b);

	ret = g_dbus_proxy_call_sync (object,
			"ListMessages",
			v,
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			cancellable,
			error);

	g_variant_builder_unref(b);
	
	//printf("*************** %s\n", ret ? g_variant_print(ret, TRUE) : "Empty");
	return ret;
}

gboolean
camel_map_dbus_update_inbox (GDBusProxy *object,
			     GCancellable *cancellable,
			     GError **error)
{
    GVariant *ret;

    ret = g_dbus_proxy_call_sync (object,
				  "UpdateInbox",
				  NULL,
				  G_DBUS_CALL_FLAGS_NONE,
				  -1,
				  cancellable,
				  error);

    return ret != NULL;
}
			     
gboolean
camel_map_dbus_set_notification_registration (GDBusProxy *object,
					      gboolean reg,
					      GCancellable *cancellable,
					      GError **error)
{
	GVariant *ret;

	ret = g_dbus_proxy_call_sync (object,
				      "SetNotificationRegistration",
				      g_variant_new ("(b)", reg),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	
	printf("REGISTRATION: %d: %d", reg, ret != NULL);
    	return ret != NULL;

}


