/*
 * e-mail-config-map-backend.c
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

#include "e-mail-config-map-backend.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include <camel/camel.h>
#include <libebackend/libebackend.h>

#include <mail/e-mail-config-auth-check.h>
#include <mail/e-mail-config-receiving-page.h>

#include "e-mail-config-map-discovery.h"
#include "utils/camel-map-settings.h"

#define E_MAIL_CONFIG_MAP_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_CONFIG_MAP_BACKEND, EMailConfigMapBackendPrivate))

struct _EMailConfigMapBackendPrivate {
	GtkWidget *view;
	GtkListStore *store;
	GList *providers;
	GtkWidget *hidden_device_name;
	GtkWidget *hidden_device_str_address;
	GtkWidget *hidden_channel;
	GtkWidget *hidden_service_name;
};

G_DEFINE_DYNAMIC_TYPE (
	EMailConfigMapBackend,
	e_mail_config_map_backend,
	E_TYPE_MAIL_CONFIG_SERVICE_BACKEND)

enum {
	COLUMN_DEVICE_NAME,
	COLUMN_DEVICE_ADDR,
	COLUMN_SERVICE_NAME,
	COLUMN_CHANNEL,
	COLUMN_PROVIDER_RECORD,
	LAST_COLUMN
};

typedef struct _AsyncContext {
	GtkWidget *button;
	EMailConfigMapBackendPrivate *priv;
	GList *providers;
	int success;
}AsyncContext;

static void
async_context_free (AsyncContext *async_context)
{
	g_slice_free (AsyncContext, async_context);
}

static void 
list_store_clear (GList **providers)
{
	GList *list = *providers;
	while (list) {
		e_mail_config_map_free_provider ((MapProvider *)list->data);
		list = list->next;
	}

	g_list_free (*providers);
	*providers = NULL;
}

static void
devices_discover_cb (gpointer object,
		     GAsyncResult *result,
		     gpointer data)
{
	GSimpleAsyncResult *simple;	
	GList *devices;
	AsyncContext *context;
	EMailConfigMapBackendPrivate *priv = (EMailConfigMapBackendPrivate *)data;	

	simple = G_SIMPLE_ASYNC_RESULT (result);
	context = g_simple_async_result_get_op_res_gpointer (simple);
	devices = context->providers;

	list_store_clear (&priv->providers);
	gtk_list_store_clear (context->priv->store);
	if (devices) {
		GList *list = context->providers;
		while (list) {
			GtkTreeIter iter;
			MapProvider *provider = (MapProvider *)list->data;

			gtk_list_store_append (context->priv->store, &iter);
			gtk_list_store_set (context->priv->store, &iter,
				COLUMN_DEVICE_NAME, provider->device_name,
				COLUMN_DEVICE_ADDR, provider->str_address,
				COLUMN_SERVICE_NAME, provider->service_name,
				COLUMN_CHANNEL, provider->channel,
				COLUMN_PROVIDER_RECORD, provider, -1);

			list = list->next;
		}

		g_list_free (context->providers);
	} else {
		GtkTreeIter iter;

		gtk_list_store_append (context->priv->store, &iter);
		gtk_list_store_set (context->priv->store, &iter,
			COLUMN_DEVICE_NAME, _("No devices found"),
			COLUMN_DEVICE_ADDR, "",
			COLUMN_SERVICE_NAME, "",
			COLUMN_CHANNEL, 0,
			COLUMN_PROVIDER_RECORD, NULL, -1);
	}
	
	gtk_widget_set_sensitive (context->button, TRUE);
	return;
}

static void
discover_devices_thread (GSimpleAsyncResult *simple,
                         GObject *object,
			 GCancellable *cancellable)
{
	AsyncContext *async_context;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);
	async_context->success = e_mail_config_map_discover_service (&async_context->providers);
}

static void 
run_discovery (GtkWidget *widget, gpointer data)
{
	EMailConfigMapBackendPrivate *priv = (EMailConfigMapBackendPrivate *)data;	
	GtkListStore *store = priv->store;
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	GCancellable *ops = camel_operation_new ();
	GtkTreeIter iter;

	list_store_clear (&priv->providers);	
	gtk_list_store_clear (store);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
		COLUMN_DEVICE_NAME, _("Searching..."),
		COLUMN_DEVICE_ADDR, "",
		COLUMN_SERVICE_NAME, "",
		COLUMN_CHANNEL, 0,
		COLUMN_PROVIDER_RECORD, NULL, -1);

	gtk_widget_set_sensitive (widget, FALSE);
	async_context = g_slice_new0 (AsyncContext);
	async_context->priv = priv;	
	async_context->providers = NULL;
	async_context->button = widget;
	async_context->success = -1;

	simple = g_simple_async_result_new (
		NULL, devices_discover_cb, data, run_discovery);
	g_simple_async_result_set_check_cancellable (simple, ops);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);	
	g_simple_async_result_run_in_thread (
		simple, discover_devices_thread, G_PRIORITY_DEFAULT, ops);

	g_object_unref (simple);

	
}

static gboolean
channel_uint_to_text (GBinding *binding,
		      const GValue *source_value,
		      GValue *target_value,
		      gpointer not_used)
{
	char *string;
	guint channel;

	channel = g_value_get_uint (source_value);
	string = g_strdup_printf ("%u", channel);
	g_value_set_string (target_value, string);
	g_free (string);

	return TRUE;
}

static gboolean
channel_text_to_uint (GBinding *binding,
		      const GValue *source_value,
		      GValue *target_value,
		      gpointer not_used)
{
	const char *str = g_value_get_string (source_value);

	if (!str || !*str)
		return FALSE;

	g_value_set_uint (target_value, (guint) strtol (str, NULL, 0));

	return TRUE;
}

static void
device_selection_changed (GtkTreeSelection *selection,
			  EMailConfigServiceBackend *backend)
{
	EMailConfigMapBackendPrivate *priv;
	MapProvider *provider;
	GtkTreeIter iter;
	GtkTreeSelection *sel;

	priv = E_MAIL_CONFIG_MAP_BACKEND_GET_PRIVATE (backend);
	sel = gtk_tree_view_get_selection (priv->view);
	if (!gtk_tree_selection_get_selected (sel, NULL, &iter))
		return;
	gtk_tree_model_get (GTK_TREE_MODEL(priv->store), &iter, COLUMN_PROVIDER_RECORD, &provider, -1);

	if (provider == NULL) {
		gtk_entry_set_text ((GtkEntry *)priv->hidden_service_name, "");
		gtk_entry_set_text ((GtkEntry *)priv->hidden_device_name, "");
		gtk_entry_set_text ((GtkEntry *)priv->hidden_device_str_address, "");
		gtk_entry_set_text ((GtkEntry *)priv->hidden_channel, "");
	} else {
		char *str = g_strdup_printf ("%u", provider->channel);
		gtk_entry_set_text ((GtkEntry *)priv->hidden_service_name, provider->service_name);
		gtk_entry_set_text ((GtkEntry *)priv->hidden_device_name, provider->device_name);
		gtk_entry_set_text ((GtkEntry *)priv->hidden_device_str_address, provider->str_address);
		gtk_entry_set_text ((GtkEntry *)priv->hidden_channel, str);
		g_free (str);
	}
	
}

static void
mail_config_map_backend_insert_widgets (EMailConfigServiceBackend *backend,
                                        GtkBox *parent)
{
	EMailConfigMapBackendPrivate *priv;
	EMailConfigServicePage *page;
	CamelSettings *settings;
	CamelMapSettings *map_settings;
	GtkWidget *widget;
	GtkWidget *container;
	GtkWidget *box;
	const gchar *text;
	gchar *markup;
	GtkListStore *store;
	GtkTreeView *view;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	const gchar *extension_name;
	ESource *source;
	ESourceExtension *extension;

	priv = E_MAIL_CONFIG_MAP_BACKEND_GET_PRIVATE (backend);
	page = e_mail_config_service_backend_get_page (backend);

	/* This backend serves double duty.  One instance holds the
	 * mail account source, another holds the mail transport source.
	 * We can differentiate by examining the EMailConfigServicePage
	 * the backend is associated with.  This method only applies to
	 * the Receiving Page. */
	if (!E_IS_MAIL_CONFIG_RECEIVING_PAGE (page))
		return;

	/* This needs to come _after_ the page type check so we don't
	 * introduce a backend extension in the mail transport source. */
	settings = e_mail_config_service_backend_get_settings (backend);
	map_settings = CAMEL_MAP_SETTINGS (settings);

	/* Hidden entries for syncing properties */
	priv->hidden_device_name = gtk_entry_new ();
	priv->hidden_device_str_address = gtk_entry_new ();
	priv->hidden_channel = gtk_entry_new ();
	priv->hidden_service_name = gtk_entry_new ();

	text = _("Configuration");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	g_object_bind_property (
		settings, "device-name",
		priv->hidden_device_name, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		settings, "device-str-address",
		priv->hidden_device_str_address, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		settings, "service-name",
		priv->hidden_service_name, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	g_object_bind_property_full (
		settings, "channel",
		priv->hidden_channel, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE,
		channel_uint_to_text,
		channel_text_to_uint,
		NULL, (GDestroyNotify) NULL);

	g_object_bind_property (
		settings, "email",
		page, "email-address",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	store = gtk_list_store_new (5,
			G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_POINTER);
	priv->store = store;
	priv->providers = NULL;
	/* Init a empty store */
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
		COLUMN_DEVICE_NAME, _("No devices found"),
		COLUMN_DEVICE_ADDR, "",
		COLUMN_SERVICE_NAME, "",
		COLUMN_CHANNEL, 0,
		COLUMN_PROVIDER_RECORD, NULL, -1);

	view = (GtkTreeView *) gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	priv->view = (GtkWidget *)view;
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes (
		_("Device name"), renderer, 
		"text", COLUMN_DEVICE_NAME, NULL);
	gtk_tree_view_append_column (view, column);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes (
		_("Device address"), renderer, 
		"text", COLUMN_DEVICE_ADDR, NULL);
	gtk_tree_view_append_column (view, column);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes (
		_("Service name"), renderer, 
		"text", COLUMN_SERVICE_NAME, NULL);
	gtk_tree_view_append_column (view, column);

	container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (view));
	gtk_widget_set_size_request (widget, -1, 200);
	gtk_widget_show (widget);
	gtk_widget_show (GTK_WIDGET(view));
	gtk_box_pack_start (GTK_BOX(container), widget, TRUE, TRUE, 12);

	widget = gtk_button_new_with_label (_("Discover"));
	g_signal_connect (widget, "clicked", G_CALLBACK(run_discovery), priv);
	gtk_widget_set_vexpand (widget, FALSE);
	gtk_widget_show (widget);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_show (box);
	gtk_box_pack_start (GTK_BOX(container), box, FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX(box), widget, TRUE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX(parent), container, FALSE, FALSE, 0);
	gtk_widget_show (container);
	
	/* Link actions */
	selection = gtk_tree_view_get_selection (view);
	g_signal_connect (selection, "changed", 
			G_CALLBACK (device_selection_changed), backend);

	/* Set initial values */
	if (camel_map_settings_get_device_name (map_settings) != NULL) {
		/* Lets load initial values to the store */
		MapProvider *provider;
		GtkTreeIter iter;

		provider = e_mail_config_map_provider_from_text (
				camel_map_settings_get_device_name (map_settings),
				camel_map_settings_get_device_str_address (map_settings),
				camel_map_settings_get_service_name (map_settings),
				camel_map_settings_get_channel (map_settings));
		gtk_list_store_clear (priv->store);
		gtk_list_store_append (priv->store, &iter);
		gtk_list_store_set (priv->store, &iter,
			COLUMN_DEVICE_NAME, provider->device_name,
			COLUMN_DEVICE_ADDR, provider->str_address,
			COLUMN_SERVICE_NAME, provider->service_name,
			COLUMN_CHANNEL, provider->channel,
			COLUMN_PROVIDER_RECORD, provider, -1);
		priv->providers = g_list_append (priv->providers, provider);
	}

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	source = e_mail_config_service_backend_get_collection (backend);
	extension = e_source_get_extension (source, extension_name);

	/* The collection identity is the user name. */
	g_object_bind_property (
		settings, "user",
		extension, "identity",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);
	
}

static void
mail_config_map_backend_setup_defaults (EMailConfigServiceBackend *backend)
{
	CamelSettings *settings;
	EMailConfigServicePage *page;
	const gchar *email_address;
	gchar **parts = NULL;

	page = e_mail_config_service_backend_get_page (backend);

	/* This backend serves double duty.  One instance holds the
	 * mail account source, another holds the mail transport source.
	 * We can differentiate by examining the EMailConfigServicePage
	 * the backend is associated with.  This method only applies to
	 * the Receiving Page. */
	if (!E_IS_MAIL_CONFIG_RECEIVING_PAGE (page))
		return;

	/* This needs to come _after_ the page type check so we don't
	 * introduce a backend extension in the mail transport source. */
	settings = e_mail_config_service_backend_get_settings (backend);

	email_address = e_mail_config_service_page_get_email_address (page);
	if (email_address != NULL)
		parts = g_strsplit (email_address, "@", 2);

	if (parts != NULL && g_strv_length (parts) >= 2) {
		CamelNetworkSettings *network_settings;

		g_strstrip (parts[0]);  /* user name */
		g_strstrip (parts[1]);  /* domain name */

		network_settings = CAMEL_NETWORK_SETTINGS (settings);
		camel_network_settings_set_user (network_settings, parts[0]);

	}

	g_strfreev (parts);
}

static gboolean
mail_config_map_backend_check_complete (EMailConfigServiceBackend *backend)
{
	EMailConfigMapBackendPrivate *priv;
	EMailConfigServicePage *page;
	CamelSettings *settings;
	CamelNetworkSettings *network_settings;
	const gchar *hosturl;
	const gchar *user;
	const char *device;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	MapProvider *provider = NULL;

	priv = E_MAIL_CONFIG_MAP_BACKEND_GET_PRIVATE (backend);	
	page = e_mail_config_service_backend_get_page (backend);

	/* This backend serves double duty.  One instance holds the
	 * mail account source, another holds the mail transport source.
	 * We can differentiate by examining the EMailConfigServicePage
	 * the backend is associated with.  This method only applies to
	 * the Receiving Page. */
	if (!E_IS_MAIL_CONFIG_RECEIVING_PAGE (page))
		return TRUE;

	device = gtk_entry_get_text (priv->hidden_device_name);
	if (device && *device)
		return TRUE;

	return FALSE;
}

static void
e_mail_config_map_backend_class_init (EMailConfigMapBackendClass *class)
{
	EMailConfigServiceBackendClass *backend_class;

	g_type_class_add_private (
		class, sizeof (EMailConfigMapBackendPrivate));

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->backend_name = "map";
	backend_class->insert_widgets = mail_config_map_backend_insert_widgets;
	backend_class->setup_defaults = mail_config_map_backend_setup_defaults;
	backend_class->check_complete = mail_config_map_backend_check_complete;
}

static void
e_mail_config_map_backend_class_finalize (EMailConfigMapBackendClass *class)
{
}

static void
e_mail_config_map_backend_init (EMailConfigMapBackend *backend)
{
	backend->priv = E_MAIL_CONFIG_MAP_BACKEND_GET_PRIVATE (backend);
}

void
e_mail_config_map_backend_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_map_backend_register_type (type_module);
}

