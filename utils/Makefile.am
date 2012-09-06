NULL =

privlib_LTLIBRARIES = libmaputils.la

libmaputils_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I..					\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS) 			\
	$(LIBEDATASERVER_CFLAGS) 		\
	$(EVOLUTION_PLUGIN_CFLAGS) 		\
	$(GNOME_INCLUDEDIR) 			\
	$(LIBEBACKEND_CFLAGS) 			\
	$(E_DATA_SERVER_CFLAGS) 		\
	$(BLUEZ_CFLAGS)				\
	-DG_LOG_DOMAIN=\"libmaputils\" 	\
	$(NULL)

libmaputils_la_SOURCES = \
	camel-map-settings.c			\
	$(NULL)

libmaputils_la_LIBADD = \
	$(CAMEL_LIBS) 				\
	$(BLUEZ_LIBS)				\
	$(EVOLUTION_PLUGIN_LIBS) 		\
	$(LIBEDATASERVER_LIBS) 			\
	$(LIBEBACKEND_LIBS) 			\
	$(E_DATA_SERVER_LIBS) 			\
	$(NULL)

libmaputils_la_LDFLAGS = $(NO_UNDEFINED)


-include $(top_srcdir)/git.mk
