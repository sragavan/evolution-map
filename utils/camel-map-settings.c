/*
 * camel-map-settings.c
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

#include "camel-map-settings.h"

#include <libedataserver/libedataserver.h>

#define CAMEL_MAP_SETTINGS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_MAP_SETTINGS, CamelMapSettingsPrivate))

struct _CamelMapSettingsPrivate {
	GMutex *property_lock;
	gboolean check_all;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
	char *email;
	char *device_name;
	char *device_str_address;
	char *service_name;
	guint channel;
};

enum {
	PROP_0,
	PROP_DEVICE_NAME,
	PROP_DEVICE_STR_ADDRESS,
	PROP_SERVICE_NAME,
	PROP_CHANNEL,
	PROP_EMAIL,
	PROP_CHECK_ALL,
	PROP_FILTER_JUNK,
	PROP_FILTER_JUNK_INBOX,
	PROP_AUTH_MECHANISM,
	PROP_HOST,
	PROP_SECURITY_METHOD,
	PROP_PORT,
	PROP_USER

};

G_DEFINE_TYPE_WITH_CODE (
	CamelMapSettings,
	camel_map_settings,
	CAMEL_TYPE_OFFLINE_SETTINGS,
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static void
map_settings_set_property (GObject *object,
                            guint property_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTH_MECHANISM:
			camel_network_settings_set_auth_mechanism (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;		
		case PROP_DEVICE_NAME :
			camel_map_settings_set_device_name(
				CAMEL_MAP_SETTINGS (object),
				g_value_get_string (value));
			return;
		case PROP_DEVICE_STR_ADDRESS:
			camel_map_settings_set_device_str_address(
				CAMEL_MAP_SETTINGS (object),
				g_value_get_string (value));
			return;
		case PROP_HOST:
			camel_network_settings_set_host (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;
			
		case PROP_SERVICE_NAME:
			camel_map_settings_set_service_name(
				CAMEL_MAP_SETTINGS (object),
				g_value_get_string (value));
			return;			
		case PROP_CHANNEL:
			camel_map_settings_set_channel (
				CAMEL_MAP_SETTINGS (object),
				g_value_get_uint (value));
			return;			
		case PROP_CHECK_ALL:
			camel_map_settings_set_check_all (
				CAMEL_MAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_EMAIL:
			camel_map_settings_set_email (
				CAMEL_MAP_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_FILTER_JUNK:
			camel_map_settings_set_filter_junk (
				CAMEL_MAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK_INBOX:
			camel_map_settings_set_filter_junk_inbox (
				CAMEL_MAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;
		case PROP_PORT:
			camel_network_settings_set_port (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_SECURITY_METHOD:
			camel_network_settings_set_security_method (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_enum (value));
			return;
		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;
			
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
map_settings_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTH_MECHANISM:
			g_value_take_string (
				value,
				camel_network_settings_dup_auth_mechanism (
				CAMEL_NETWORK_SETTINGS (object)));
			return;		
		case PROP_DEVICE_NAME:
			g_value_take_string (
				value,
				g_strdup(camel_map_settings_get_device_name(
				CAMEL_MAP_SETTINGS (object))));
			return;

		case PROP_HOST:
			g_value_take_string (
				value,
				camel_network_settings_dup_host (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_DEVICE_STR_ADDRESS:
			g_value_take_string (
				value,
				g_strdup(camel_map_settings_get_device_str_address(
				CAMEL_MAP_SETTINGS (object))));
			return;
		case PROP_SERVICE_NAME:
			g_value_take_string (
				value,
				g_strdup(camel_map_settings_get_service_name(
				CAMEL_MAP_SETTINGS (object))));
			return;
		case PROP_CHANNEL:
			g_value_set_uint (
				value,
				camel_map_settings_get_channel(
				CAMEL_MAP_SETTINGS (object)));
			return;
		case PROP_CHECK_ALL:
			g_value_set_boolean (
				value,
				camel_map_settings_get_check_all (
				CAMEL_MAP_SETTINGS (object)));
			return;

		case PROP_EMAIL:
			g_value_take_string (
				value,
				g_strdup(camel_map_settings_get_email (
				CAMEL_MAP_SETTINGS (object))));
			return;

		case PROP_FILTER_JUNK:
			g_value_set_boolean (
				value,
				camel_map_settings_get_filter_junk (
				CAMEL_MAP_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK_INBOX:
			g_value_set_boolean (
				value,
				camel_map_settings_get_filter_junk_inbox (
				CAMEL_MAP_SETTINGS (object)));
			return;
		case PROP_USER:
			g_value_take_string (
				value,
				camel_network_settings_dup_user (
				CAMEL_NETWORK_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
map_settings_finalize (GObject *object)
{
	CamelMapSettingsPrivate *priv;

	priv = CAMEL_MAP_SETTINGS_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	g_free (priv->email);
	g_free (priv->device_name);
	g_free (priv->device_str_address);
	g_free (priv->service_name);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_map_settings_parent_class)->finalize (object);
}

static void
camel_map_settings_class_init (CamelMapSettingsClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelMapSettingsPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = map_settings_set_property;
	object_class->get_property = map_settings_get_property;
	object_class->finalize = map_settings_finalize;

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_AUTH_MECHANISM,
		"auth-mechanism");
	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_HOST,
		"host");
	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_PORT,
		"port");
	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_SECURITY_METHOD,
		"security-method");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_USER,
		"user");

	g_object_class_install_property (
		object_class,
		PROP_CHECK_ALL,
		g_param_spec_boolean (
			"check-all",
			"Check All",
			"Check all folders for new messages",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_EMAIL,
		g_param_spec_string (
			"email",
			"Email",
			"Email",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (
		object_class,
		PROP_DEVICE_NAME,
		g_param_spec_string (
			"device-name",
			"Device Name",
			"Device Name",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (
		object_class,
		PROP_DEVICE_STR_ADDRESS,
		g_param_spec_string (
			"device-str-address",
			"Device string address",
			"Device string address",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (
		object_class,
		PROP_SERVICE_NAME,
		g_param_spec_string (
			"service-name",
			"Service Name",
			"Service Name",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (
		object_class,
		PROP_CHANNEL,
		g_param_spec_uint (
			"channel",
			"Channel",
			"Channel",
			0, G_MAXUINT, 0,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (
		object_class,
		PROP_FILTER_JUNK,
		g_param_spec_boolean (
			"filter-junk",
			"Filter Junk",
			"Whether to filter junk from all folders",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_JUNK_INBOX,
		g_param_spec_boolean (
			"filter-junk-inbox",
			"Filter Junk Inbox",
			"Whether to filter junk from Inbox only",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_map_settings_init (CamelMapSettings *settings)
{
	settings->priv = CAMEL_MAP_SETTINGS_GET_PRIVATE (settings);
	settings->priv->property_lock = g_mutex_new ();
}

/**
 * camel_map_settings_get_check_all:
 * @settings: a #CamelMapSettings
 *
 * Returns whether to check all folders for new messages.
 *
 * Returns: whether to check all folders for new messages
 *
 * Since: 3.4
 **/
gboolean
camel_map_settings_get_check_all (CamelMapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_MAP_SETTINGS (settings), FALSE);

	return settings->priv->check_all;
}

/**
 * camel_map_settings_set_check_all:
 * @settings: a #CamelMapSettings
 * @check_all: whether to check all folders for new messages
 *
 * Sets whether to check all folders for new messages.
 *
 * Since: 3.4
 **/
void
camel_map_settings_set_check_all (CamelMapSettings *settings,
                                  gboolean check_all)
{
	g_return_if_fail (CAMEL_IS_MAP_SETTINGS (settings));

	if ((settings->priv->check_all ? 1 : 0) == (check_all ? 1 : 0))
		return;

	settings->priv->check_all = check_all;

	g_object_notify (G_OBJECT (settings), "check-all");
}

const gchar *
camel_map_settings_get_email (CamelMapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_MAP_SETTINGS (settings), NULL);

	return settings->priv->email;
}


void
camel_map_settings_set_email (CamelMapSettings *settings,
                              const gchar *email)
{
	g_return_if_fail (CAMEL_IS_MAP_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->email, email) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->email);
	settings->priv->email = e_util_strdup_strip (email);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "email");
}

const gchar *
camel_map_settings_get_device_name (CamelMapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_MAP_SETTINGS (settings), NULL);

	return settings->priv->device_name;
}


void
camel_map_settings_set_device_name (CamelMapSettings *settings,
                              	    const gchar *name)
{
	g_return_if_fail (CAMEL_IS_MAP_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->device_name, name) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->device_name);
	settings->priv->device_name = g_strdup (name);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "device-name");
}

const gchar *
camel_map_settings_get_device_str_address (CamelMapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_MAP_SETTINGS (settings), NULL);

	return settings->priv->device_str_address;
}


void
camel_map_settings_set_device_str_address (CamelMapSettings *settings,
                              		   const gchar *address)
{
	g_return_if_fail (CAMEL_IS_MAP_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->device_str_address, address) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->device_str_address);
	settings->priv->device_str_address = g_strdup (address);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "device-str-address");
}

const gchar *
camel_map_settings_get_service_name (CamelMapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_MAP_SETTINGS (settings), NULL);

	return settings->priv->service_name;
}


void
camel_map_settings_set_service_name (CamelMapSettings *settings,
                              	     const gchar *service)
{
	g_return_if_fail (CAMEL_IS_MAP_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->service_name, service) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->service_name);
	settings->priv->service_name = g_strdup (service);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "service-name");
}

/**
 * camel_map_settings_get_filter_junk:
 * @settings: a #CamelMapSettings
 *
 * Returns whether to automatically find and tag junk messages amongst new
 * messages in all folders.
 *
 * Returns: whether to filter junk in all folders
 *
 * Since: 3.4
 **/
gboolean
camel_map_settings_get_filter_junk (CamelMapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_MAP_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk;
}

/**
 * camel_map_settings_set_filter_junk:
 * @settings: a #CamelMapSettings
 * @filter_junk: whether to filter junk in all filers
 *
 * Sets whether to automatically find and tag junk messages amongst new
 * messages in all folders.
 *
 * Since: 3.4
 **/
void
camel_map_settings_set_filter_junk (CamelMapSettings *settings,
                                    gboolean filter_junk)
{
	g_return_if_fail (CAMEL_IS_MAP_SETTINGS (settings));

	if ((settings->priv->filter_junk ? 1 : 0) == (filter_junk ? 1 : 0))
		return;

	settings->priv->filter_junk = filter_junk;

	g_object_notify (G_OBJECT (settings), "filter-junk");
}

/**
 * camel_map_settings_get_filter_junk_inbox:
 * @settings: a #CamelMapSettings
 *
 * Returns whether to automatically find and tag junk messages amongst new
 * messages in the Inbox folder only.
 *
 * Returns: whether to filter junk in Inbox only
 *
 * Since: 3.4
 **/
gboolean
camel_map_settings_get_filter_junk_inbox (CamelMapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_MAP_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk_inbox;
}

/**
 * camel_map_settings_set_filter_junk_inbox:
 * @settings: a #CamelMapSettings
 * @filter_junk_inbox: whether to filter junk in Inbox only
 *
 * Sets whether to automatically find and tag junk messages amongst new
 * messages in the Inbox folder only.
 *
 * Since: 3.4
 **/
void
camel_map_settings_set_filter_junk_inbox (CamelMapSettings *settings,
                                          gboolean filter_junk_inbox)
{
	g_return_if_fail (CAMEL_IS_MAP_SETTINGS (settings));

	if ((settings->priv->filter_junk_inbox ? 1 : 0) == (filter_junk_inbox ? 1 : 0))
		return;

	settings->priv->filter_junk_inbox = filter_junk_inbox;

	g_object_notify (G_OBJECT (settings), "filter-junk-inbox");
}


guint
camel_map_settings_get_channel (CamelMapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_MAP_SETTINGS (settings), 0);

	return settings->priv->channel;
}

void
camel_map_settings_set_channel (CamelMapSettings *settings,
                                guint channel)
{
	g_return_if_fail (CAMEL_IS_MAP_SETTINGS (settings));

	if (settings->priv->channel == channel)
		return;

	settings->priv->channel = channel;

	g_object_notify (G_OBJECT (settings), "channel");
}
