camel_provider_LTLIBRARIES = libcamelmap.la
camel_provider_DATA = libcamelmap.urls

libcamelmap_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I..					\
	-I$(srcdir)/..				\
	-I$(top_srcdir)/camel			\
	$(CAMEL_CFLAGS) 			\
	$(LIBEDATASERVER_CFLAGS) 		\
	$(EVOLUTION_PLUGIN_CFLAGS) 		\
	$(GNOME_INCLUDEDIR) 			\
	$(LIBEBACKEND_CFLAGS) 			\
	$(E_DATA_SERVER_CFLAGS) 		\
	$(BLUEZ_CFLAGS)				\
	-DG_LOG_DOMAIN=\"camel-map-provider\" 	\
	$(NULL)
	
libcamelmap_la_SOURCES =			\
	camel-map-provider.c			\
	camel-map-store.c			\
	camel-map-store-summary.c		\
	camel-map-dbus-utils.c			\
	camel-map-summary.c			\
	camel-map-folder.c

libcamelmap_la_LIBADD = \
	$(top_srcdir)/utils/libmaputils.la	\
	$(CAMEL_LIBS) 				\
	$(BLUEZ_LIBS)				\
	$(EVOLUTION_PLUGIN_LIBS) 		\
	$(LIBEDATASERVER_LIBS) 			\
	$(LIBEBACKEND_LIBS) 			\
	$(E_DATA_SERVER_LIBS) 			\
	$(NULL)

libcamelmap_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED) \
	$(NULL)

EXTRA_DIST = libcamelmap.urls

-include $(top_srcdir)/git.mk
