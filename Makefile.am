SUBDIRS = src po docs

gamidocdir = $(datadir)/doc/gami
gamidoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgami-1.0.pc

EXTRA_DIST = \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	libgami-1.0.pc.in

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update \
	libgami-1.0.pc

DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if USE_SVN2CL
	@SVN2CL@ --group-by-day -o $(distdir)/ChangeLog
endif

install-data-hook:
if USE_SVN2CL
	@SVN2CL@ --group-by-day -o $(gamidocdir)/ChangeLog
endif
