AC_PREREQ(2.58)
AC_INIT([evolution-map], [3.5.91], [http://bugzilla.gnome.org/browse.cgi?product=evolution-map])
AM_INIT_AUTOMAKE([gnu 1.9 dist-xz no-dist-gzip -Wno-portability])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR(README)
AC_CONFIG_HEADERS(config.h)

dnl ****************************
dnl Evolution-MAP API version
dnl ****************************
m4_define([EVO_MAP_API_VERSION_MACRO], [1.0])
EVO_MAP_API_VERSION=EVO_MAP_API_VERSION_MACRO
AC_SUBST(EVO_MAP_API_VERSION)


dnl *******************
dnl Required Packages
dnl *******************
m4_define([eds_minimum_version], [3.2.0])
m4_define([evo_minimum_version], [3.2.0])
m4_define([glib_minimum_version], [2.28])
m4_define([gtk_minimum_version], [3.0])


dnl ***********************************
dnl Automake 1.11 - Silent Build Rules
dnl ***********************************
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl ****************************
dnl Initialize maintainer mode
dnl ****************************
AM_MAINTAINER_MODE([enable])

AC_PROG_CC
AC_PROG_CPP
AC_C_INLINE
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

dnl ******************************
dnl Check for Win32
dnl ******************************

AC_MSG_CHECKING([for Win32])
case "$host" in
*-mingw*)
	os_win32=yes
	NO_UNDEFINED='-no-undefined'
	SOCKET_LIBS='-lws2_32'
	;;
*)
	os_win32=no
	NO_UNDEFINED='-no-undefined'
	SOCKET_LIBS=''
	;;
esac

AC_MSG_RESULT([$os_win32])
AM_CONDITIONAL(OS_WIN32, [test "x$os_win32" = "xyes"])
AC_SUBST(NO_UNDEFINED)
AC_SUBST(SOCKET_LIBS)

dnl ***********************
dnl Compiler Warning Flags
dnl ***********************
AS_COMPILER_FLAGS(WARNING_FLAGS,
	"-Wall -Wextra
	-Wno-missing-field-initializers
	-Wno-sign-compare
	-Wno-unused-parameter
	-Wno-deprecated-declarations
	-Wdeclaration-after-statement
	-Werror-implicit-function-declaration
	-Wformat-nonliteral -Wformat-security -Winit-self
	-Wmissing-declarations -Wmissing-include-dirs
	-Wmissing-noreturn -Wnested-externs -Wpointer-arith
	-Wundef -Wwrite-strings")
AC_SUBST(WARNING_FLAGS)

dnl Other useful compiler warnings for test builds only.
dnl These may produce warnings we have no control over.
dnl
dnl	-Wmissing-format-attribute
dnl	-Wshadow
dnl	-Wredundant-decls

CFLAGS="$CFLAGS $WARNING_FLAGS"

dnl ************
dnl i18n stuff
dnl ************
IT_PROG_INTLTOOL([0.35.5])
AM_GLIB_GNU_GETTEXT
AM_PATH_GLIB_2_0(2.26.0,,,gobject gthread)

GETTEXT_PACKAGE=evolution-map
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Package name for gettext])

localedir='$(prefix)/$(DATADIRNAME)/locale'
AC_SUBST(localedir)

dnl ****************************
dnl Initialize libtool
dnl ****************************
AM_DISABLE_STATIC
AC_PROG_LIBTOOL

PKG_PROG_PKG_CONFIG

dnl ********************************
dnl Check for evolution-data-server
dnl ********************************
PKG_CHECK_MODULES(EVOLUTION_DATA_SERVER, evolution-data-server-1.2 >= eds_minimum_version)

dnl ********************************
dnl Check for Bluez
dnl ********************************
PKG_CHECK_MODULES(BLUEZ, bluez)


dnl *****************************
dnl Check for GNOME Platform
dnl *****************************
PKG_CHECK_MODULES(GNOME_PLATFORM,
	[glib-2.0 >= glib_minimum_version
	 gtk+-3.0 >= gtk_minimum_version])

PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= libsoup_minimum_version)
AC_SUBST(SOUP_CFLAGS)
AC_SUBST(SOUP_LIBS)
LIBSOUP_REQUIRED=libsoup_minimum_version
AC_SUBST(LIBSOUP_REQUIRED)

dnl ********************************
dnl Check for remaining evolution-data-server modules
dnl ********************************
PKG_CHECK_MODULES(LIBEDATASERVER, libedataserver-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATASERVERUI, libedataserverui-3.0 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEBACKEND, libebackend-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(CAMEL, camel-1.2 >= eds_minimum_version)


dnl ****************************
dnl Check for evolution plugins 
dnl ****************************
AC_ARG_ENABLE([eplugin],
	[AS_HELP_STRING([--enable-eplugin],
	[enable Evolution plugin support (default=no)])],
	[enable_eplugin=$enableval], [enable_eplugin=no])
AC_MSG_CHECKING([if Evolution plugins is enabled])
AC_MSG_RESULT([$enable_eplugin])
if test "x$enable_eplugin" = xyes; then

	PKG_CHECK_MODULES(EVOLUTION_SHELL, evolution-shell-3.0 >= evo_minimum_version)
	PKG_CHECK_MODULES(EVOLUTION_MAIL, evolution-mail-3.0 >= evo_minimum_version)
	PKG_CHECK_MODULES(LIBEMAIL_ENGINE, libemail-engine >= eds_minimum_version)
	PKG_CHECK_MODULES(EVOLUTION_PLUGIN, evolution-plugin-3.0 >= evo_minimum_version)
fi
AM_CONDITIONAL([ENABLE_EPLUGIN], [test "x$enable_eplugin" = xyes])

dnl ****************************
dnl Check for sqlite3 
dnl ****************************
PKG_CHECK_MODULES(SQLITE3, sqlite3)

EDS_REQUIRED=eds_minimum_version
AC_SUBST(EDS_REQUIRED)

EVO_REQUIRED=evo_minimum_version
AC_SUBST(EVO_REQUIRED)

dnl *******************************
dnl Add evolution plugin rules here
dnl *******************************
EVO_PLUGIN_RULE=$srcdir/eplugin-rule.mk
AC_SUBST_FILE(EVO_PLUGIN_RULE)

dnl *******************
dnl Special directories
dnl *******************
if test "x$enable_eplugin" = xyes; then
	errordir=`$PKG_CONFIG --variable=errordir evolution-plugin-3.0`
	AC_SUBST(errordir)
	plugindir=`$PKG_CONFIG --variable=plugindir evolution-plugin-3.0`
	AC_SUBST(plugindir)
	evo_moduledir=`$PKG_CONFIG --variable=moduledir evolution-shell-3.0`
	AC_SUBST(evo_moduledir)
fi

camel_providerdir=`$PKG_CONFIG --variable=camel_providerdir camel-1.2`
AC_SUBST(camel_providerdir) 

privincludedir=`$PKG_CONFIG --variable=privincludedir libedataserver-1.2`
AC_SUBST(privincludedir)

privlibdir=`$PKG_CONFIG --variable=privlibdir evolution-data-server-1.2`
AC_SUBST(privlibdir)

edataserver_privincludedir=`$PKG_CONFIG --variable=privincludedir libedataserver-1.2`
AC_SUBST(edataserver_privincludedir)

eds_moduledir=`$PKG_CONFIG --variable=moduledir libebackend-1.2`
AC_SUBST(eds_moduledir)


dnl *************
dnl Gtk Doc stuff
dnl *************
GTK_DOC_CHECK(1.9)

dnl **************************
dnl Makefiles, generated .pc's
dnl **************************
AC_CONFIG_FILES([
Makefile 
camel/Makefile
configuration/Makefile
utils/Makefile
po/Makefile.in
])
AC_OUTPUT

AC_MSG_NOTICE([
])

