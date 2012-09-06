NULL =

evo_module_LTLIBRARIES = module-map-configuration.la

module_map_configuration_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/utils \
	$(EVOLUTION_MAIL_CFLAGS) \
	$(EVOLUTION_SHELL_CFLAGS) \
	$(NULL)

module_map_configuration_la_SOURCES = 	\
	module-map-configuration.c 	\
	e-mail-config-map-backend.c 	\
	e-mail-config-map-backend.h	\
	e-mail-config-map-discovery.c	\
	e-mail-config-map-discovery.h	\
       $(NULL)	


module_map_configuration_la_LIBADD = \
	$(top_srcdir)/utils/libmaputils.la	\
	$(EVOLUTION_MAIL_LIBS) 			\
	$(EVOLUTION_SHELL_LIBS) 		\
	$(NULL)

module_map_configuration_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED)

error_DATA = module-map-configuration.error

%.error: %.error.xml
	$(AM_V_GEN) LC_ALL=C $(INTLTOOL_MERGE) -x -u /tmp/notthere $< $@

EXTRA_DIST = module-map-configuration.error.xml

BUILT_SOURCES = $(error_DATA)

CLEANFILES = $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
