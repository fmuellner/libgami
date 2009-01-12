SUBDIRS = gami po docs/reference

gamidocdir = $(datadir)/doc/gami
gamidoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gami-1.0.pc

EXTRA_DIST = \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	gami-1.0.pc.in

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update \
	gami-1.0.pc

DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

# Copy all the spec files. Of cource, only one is actually used.
dist-hook:
	for specfile in *.spec; do \
		if test -f $$specfile; then \
			cp -p $$specfile $(distdir); \
		fi \
	done
if USE_SVN2CL
	@SVN2CL@ --group-by-day -o $(distdir)/ChangeLog

install-data-hook:
	@SVN2CL@ --group-by-day -o $(gamidocdir)/ChangeLog
endif
