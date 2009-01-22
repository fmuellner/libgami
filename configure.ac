dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(gami, 0.2)

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_HEADERS([config.h])

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC

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
AM_PROG_LIBTOOL

PKG_CHECK_MODULES(GAMI, [glib-2.0 gobject-2.0])


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
