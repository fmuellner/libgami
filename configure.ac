dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(gami, 0.3)
AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_HEADERS([config.h])

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC


dnl ***************************************************************************
dnl Win32 + gai_strerror
dnl ***************************************************************************
AC_MSG_CHECKING([for Win32])
case "$host" in
  *-*-mingw*)
    AC_MSG_RESULT([yes])

    AC_DEFINE([WINVER],[0x0501],[Set required API version when on windows])
    LDFLAGS="$LDFLAGS -lws2_32 -no-undefined"

    AC_MSG_CHECKING([for gai_strerror])
    AC_LINK_IFELSE(
        [AC_LANG_PROGRAM([[
                #include <windef.h>
                #include <ws2tcpip.h>
            ]],[[
                gai_strerror (0);
            ]])
        ],
        [have_gai_strerror=yes],
        [have_gai_strerror=no])
    AC_MSG_RESULT([$have_gai_strerror])
  ;;
  *)
    AC_MSG_RESULT([no])

    AC_CHECK_FUNC([gai_strerror],
	[have_gai_strerror=yes],
	[have_gai_strerror=no])
  ;;
esac


if test "$have_gai_strerror" = "yes";
then
	AC_DEFINE([HAVE_GAI_STRERROR],[1],
		[Define if a usable gai_strerror exists])
fi


dnl ***************************************************************************
dnl Internatinalization
dnl ***************************************************************************
GETTEXT_PACKAGE=gami
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [GETTEXT package name])
ALL_LINGUAS="`grep -v '^#' "$srcdir/po/LINGUAS" | tr '\n' ' '`"
AM_GLIB_GNU_GETTEXT
AM_GLIB_DEFINE_LOCALEDIR(GAMI_LOCALEDIR)
IT_PROG_INTLTOOL([0.35.0])
 
##################################################
# Libtool
##################################################
m4_defun([_LT_AC_LANG_CXX_CONFIG], [:]) dnl disable checks for C++
m4_defun([_LT_AC_LANG_F77_CONFIG], [:]) dnl disable checks for Fortran
m4_defun([_LT_AC_LANG_GCJ_CONFIG], [:]) dnl disable checks for (GNU) Java
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL

PKG_CHECK_MODULES(GAMI, [glib-2.0 gobject-2.0])

##################################################
# GObject Introspection
##################################################
have_introspection=false
PKG_CHECK_MODULES(INTROSPECTION, gobject-introspection-1.0 >= 0.6.2,
		  have_introspection=true, have_introspection=false)
AM_CONDITIONAL(HAVE_INTROSPECTION, $have_introspection)

G_IR_SCANNER=
G_IR_COMPILER=
G_IR_GENERATE=
GIRDIR=
TYPELIBDIR=
if $have_introspection; then
  G_IR_SCANNER=`$PKG_CONFIG --variable=g_ir_scanner gobject-introspection-1.0`
  G_IR_COMPILER=`$PKG_CONFIG --variable=g_ir_compiler gobject-introspection-1.0`
  G_IR_GENERATE=`$PKG_CONFIG --variable=g_ir_generate gobject-introspection-1.0`
  GIRDIR=`$PKG_CONFIG --variable=girdir gobject-introspection-1.0`
  TYPELIBDIR=`$PKG_CONFIG --variable=typelibdir gobject-introspection-1.0`
fi
AC_SUBST(G_IR_SCANNER)
AC_SUBST(G_IR_COMPILER)
AC_SUBST(G_IR_GENERATE)
AC_SUBST(GIRDIR)
AC_SUBST(TYPELIBDIR)


##################################################
# Check for gtk-doc.
##################################################
GTK_DOC_CHECK([1.9])

dnl Check for svn2cl for some ChangeLog magic
AC_PATH_PROG(SVN2CL,svn2cl,no)

if test x"$SVN2CL" != xno; then
	AC_MSG_CHECKING([if $SVN2CL works as expected])
	AS_IF([$SVN2CL --stdout >/dev/null 2>&1],[USE_SVN2CL=yes],
	      [USE_SVN2CL=no])
	AC_MSG_RESULT([$USE_SVN2CL])
fi

AM_CONDITIONAL(USE_SVN2CL, test x"$USE_SVN2CL" = xyes)

AC_OUTPUT([
Makefile
gami-1.0.pc
src/Makefile
po/Makefile.in
docs/Makefile
docs/reference/Makefile
docs/reference/version.xml
])
