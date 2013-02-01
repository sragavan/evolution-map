
#include <string.h>
#include <camel/camel.h>
#include <libedataserver/libedataserver.h>

static void
print_folder_info (CamelFolderInfo *info, int i)
{
	while (info) {
		int j;

		for (j=0; j<i; j++)
			printf("   ");
		printf("%s (%d/%d)\n", info->display_name, info->unread, info->total);
		print_folder_info (info->child, i+1);
		info = info->next;
	}
}
gint
main (gint argc,
      gchar *argv[])
{
	CamelSession *session;
	CamelService *service;
	ESourceRegistry *registry;
	GList *list, *link;
	const gchar *extension_name;

	g_type_init ();
	
	system ("rm -rf /tmp/test-map");
	camel_init ("/tmp/test-map", TRUE);
	e_source_camel_register_types ();

	session = g_object_new (
		CAMEL_TYPE_SESSION,
		"user-data-dir", "/tmp/test-map", 
		"user-cache-dir", "/tmp/test-map/cache", NULL);


	/* Browse through the ESource registry to find out the MAP account */
	registry = e_source_registry_new_sync (NULL, NULL);
	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	list = e_source_registry_list_sources (registry, extension_name);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *source = E_SOURCE (link->data);
		const gchar *uid;
		const gchar *backend_name = "INVALID";
		const gchar *display_name;
		ESourceBackend *extension;
		
		if (!e_source_get_enabled (source))
			continue;
		uid = e_source_get_uid (source);
		display_name = e_source_get_display_name (source);
		printf("Looking for: %s\n", display_name);

		extension = e_source_get_extension (source, extension_name);
		backend_name = e_source_backend_get_backend_name (extension);

		if (strcmp (backend_name, "map") == 0) {
			/* Lets add just map backend to the session. */
			CamelFolderInfo *info;
			CamelFolder *folder;
			GPtrArray *uids;
			int i;
			GError *error=NULL;
			service = camel_session_add_service (
				CAMEL_SESSION (session), uid,
				backend_name, CAMEL_PROVIDER_STORE, NULL);
			e_source_camel_configure_service (source, service);
			camel_service_connect_sync (service, NULL, NULL);
			info = camel_store_get_folder_info_sync (
				CAMEL_STORE (service), "", 0, NULL, NULL);
			print_folder_info (info, 0);
			folder = camel_store_get_folder_sync (
				CAMEL_STORE (service), "inbox", 0, NULL, NULL);
			camel_folder_refresh_info_sync (folder, NULL, &error);
			if (error)
				printf("Refresh info failed: %s\n", error->message);

			uids = camel_folder_get_uids (folder);
			printf("Length of messages: %p %d\n", folder, uids->len);
			for (i=0; i<uids->len; i++)
				printf("UID: %s\n", (char *)uids->pdata[i]);
		}
		


	}
	
	return 0;
}

