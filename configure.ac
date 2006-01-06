AC_INIT
AC_CANONICAL_TARGET([])

dnl need to do this here so intltool-update doesn't die a slow but awful death
PACKAGE=byzanz
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION($PACKAGE, TOOLS_VERSION, 0, 0, 0, 1, BYZANZ_CVS="no", BYZANZ_CVS="yes")

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE
AM_INIT_AUTOMAKE($PACKAGE,$VERSION)
AC_PROG_LIBTOOL

AC_CONFIG_SRCDIR([gifenc/gifenc.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
AC_CONFIG_MACRO_DIR(macros)
AC_SUBST(ACLOCAL_AMFLAGS, "-I macros")
AC_PROG_INTLTOOL

dnl the gettext stuff needed
ALL_LINGUAS="de"
AM_GNU_GETTEXT_VERSION(0.12)
AM_GNU_GETTEXT([external])

GETTEXT_PACKAGE=$PACKAGE
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_DIR(GNOMELOCALEDIR, "${datadir}/locale", [locale directory])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], "$GETTEXT_PACKAGE",
                   [gettext package name])

dnl define LOCALEDIR in config.h
AS_AC_EXPAND(LOCALEDIR, $datadir/locale)
AC_DEFINE_UNQUOTED([LOCALEDIR], "$LOCALEDIR",
                   [gettext locale dir])


AC_PROG_CC
dnl For interactive UNIX (a Sun thing)
AC_ISC_POSIX

dnl decide on error flags
dnl if we support -Wall, set it unconditionally
AS_COMPILER_FLAG(-Wall,
                 ERROR_CFLAGS="-Wall",
                 ERROR_CFLAGS="")
dnl if we're in nano >= 1, add -Werror if supported
if test "x$BYZANZ_CVS" = "xyes"
then
  AS_COMPILER_FLAG(-Werror, ERROR_CFLAGS="$ERROR_CFLAGS -Werror")
fi

AC_HEADER_STDC([])
AC_C_INLINE

dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],
                  [colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

dnl Check for a way to display the function name in debug output
BYZANZ_CHECK_FUNCTION()

AC_ARG_ENABLE(gcov,
  AC_HELP_STRING([--enable-gcov],[compile with coverage profiling instrumentation (gcc only)]),
  enable_gcov=$enableval,enable_gcov=no)
if test x$enable_gcov = xyes ; then
  AS_COMPILER_FLAG(["-fprofile-arcs"],
    [BYZANZ_INT_CFLAGS="$BYZANZ_INT_CFLAGS -fprofile-arcs"],
    true)
  AS_COMPILER_FLAG(["-ftest-coverage"],
    [BYZANZ_INT_CFLAGS="$BYZANZ_INT_CFLAGS -ftest-coverage"],
    true)
  BYZANZ_INT_CFLAGS=`echo "$BYZANZ_INT_CFLAGS" | sed -e 's/-O[0-9]*//g'`

  AC_DEFINE_UNQUOTED(BYZANZ_GCOV_ENABLED, 1, [Defined if gcov is enabled to force a rebuild due to config.h changing])
fi
AM_CONDITIONAL(BYZANZ_GCOV_ENABLED, test x$enable_gcov = xyes)

dnl HAVE_WIN32 currently means "disable POSIXisms".
case "$host" in
  *-*-mingw*)
    AC_DEFINE_UNQUOTED(HAVE_WIN32, 1, [Defined if compiling for Windows])
    ;;
  *)
    ;;
esac

dnl Check for essential libraries first:
dnl ====================================

GTK_REQ="2.6.0"
GTHREAD_REQ="2.6.0"
APPLET_REQ="2.10.0"

PKG_CHECK_MODULES(GTK, gtk+-2.0 >= $GTK_REQ,HAVE_GTK=yes,HAVE_GTK=no)
if test "x$HAVE_GTK" = "xno"; then
  AC_MSG_ERROR([Byzanz requires Gtk+-2.0 >= $GTK_REQ to compile.])
fi

PKG_CHECK_MODULES(GTHREAD, gthread-2.0 >= $GTHREAD_REQ,HAVE_GTHREAD=yes,HAVE_GTHREAD=no)
if test "x$HAVE_GTHREAD" = "xno"; then
  AC_MSG_ERROR([Byzanz requires GThread-2.0 >= $GLIB_REQ to compile.])
fi

PKG_CHECK_MODULES(APPLET, libpanelapplet-2.0 >= $APPLET_REQ,HAVE_APPLET=yes,HAVE_APPLET=no)
if test "x$HAVE_APPLET" = "xno"; then
  AC_MSG_ERROR([Byzanz requires libpanelapplet-2.0 >= $APPLET_REQ to compile.])
fi

GIFENC_CFLAGS="$GTK_CFLAGS $ERROR_CFLAGS"
GIFENC_LIBS="$GTK_LIBS"
AS_SCRUB_INCLUDE(GIFENC_CFLAGS)
AC_SUBST(GIFENC_CFLAGS)
AC_SUBST(GIFENC_LIBS)

BYZANZ_CFLAGS="$GTK_CFLAGS $GTHREAD_CFLAGS $ERROR_CFLAGS"
dnl FIXME FIXME FIXME
BYZANZ_LIBS="$GTK_LIBS $GTHREAD_LIBS -lX11 -L/usr/X11R6/lib -lXdamage -lXfixes"
AS_SCRUB_INCLUDE(BYZANZ_CFLAGS)
AC_SUBST(BYZANZ_CFLAGS)
AC_SUBST(BYZANZ_LIBS)

APPLET_CFLAGS="$APPLET_CFLAGS $BYZANZ_CFLAGS"
APPLET_LIBS="$APPLET_LIBS $BYZANZ_LIBS"
AS_SCRUB_INCLUDE(APPLET_CFLAGS)

dnl #########################
dnl # Make the output files #
dnl #########################

AC_OUTPUT(
Makefile
gifenc/Makefile
macros/Makefile
po/Makefile.in
src/Makefile
)

