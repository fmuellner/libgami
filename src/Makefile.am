NULL =

EXTRA_DIST = gami.c

DISTCLEANFILES =

GAMI_DEBUG_FLAGS = -Wall -g

AM_CFLAGS =                        \
	-DG_LOG_DOMAIN=\"Gami\"    \
	$(GAMI_CFLAGS)             \
	$(GAMI_DEBUG_FLAGS)        \
	-DGAMI_COMPILATION         \
	-DG_DISABLE_DEPRECATED

lib_LTLIBRARIES = libgami-1.0.la

libgami_1_0_la_SOURCES =          \
        $(srcdir)/gami-main.c               \
        $(srcdir)/gami-main.h               \
        $(srcdir)/gami-manager.c            \
        $(srcdir)/gami-manager.h            \
        $(srcdir)/gami-manager-types.h      \
        $(srcdir)/gami-manager-types.c      \
        $(srcdir)/gami-manager-private.c    \
        $(srcdir)/gami-manager-private.h    \
        $(srcdir)/gami-enums.h              \
        $(srcdir)/gami-enumtypes.c          \
        $(srcdir)/gami-enumtypes.h          \
        $(srcdir)/gami-error.c              \
        $(srcdir)/gami-error.h              \
        $(NULL)

libgami_1_0_la_LDFLAGS = $(GAMI_LIBS)

gamiincludedir=$(includedir)/libgami-1.0
gamiinclude_HEADERS = gami.h

gami_headers =                    \
	$(srcdir)/gami-main.h               \
	$(srcdir)/gami-manager.h            \
	$(srcdir)/gami-manager-types.h      \
	$(srcdir)/gami-enums.h              \
	$(srcdir)/gami-error.h              \
	$(NULL)

gamisubincludedir=$(gamiincludedir)/gami
gamisubinclude_HEADERS =          \
	$(gami_headers)               \
	$(srcdir)/gami-enumtypes.h    \
	$(NULL)


BUILT_SOURCES =                    \
        $(srcdir)/gami-enumtypes.h \
        $(srcdir)/gami-enumtypes.c \
        $(NULL)

EXTRA_DIST +=                     \
        gami-enumtypes.h.template \
        gami-enumtypes.c.template \
        $(NULL)

gami-enumtypes.h: gami-enumtypes.h.template $(gami_headers)
	$(AM_V_GEN) glib-mkenums --template $< $(gami_headers) >$@.tmp && \
	mv $@.tmp $@

gami-enumtypes.c: gami-enumtypes.c.template $(gami_headers)
	$(AM_V_GEN) glib-mkenums --template $< $(gami_headers) >$@.tmp && \
	mv $@.tmp $@

CLEANFILES = $(BUILT_SOURCES)

if HAVE_INTROSPECTION

Gami-1.0.gir: libgami-1.0.la Makefile gami.h
	$(AM_V_GEN)$(INTROSPECTION_SCANNER) \
		--namespace Gami --nsversion=1.0 \
		--add-include-path=$(top_srcdir)/src --add-include-path=. \
		--output $@ \
		--libtool="$(LIBTOOL)" \
		--c-include=gami.h \
		--include=GObject-2.0 \
		--include=Gio-2.0 \
		--library=gami-1.0 \
		--pkg gobject-2.0 \
		--pkg gio-2.0 \
		-I$(top_srcdir)/src \
		$(GAMI_CFLAGS) \
		-DGAMI_COMPILATION \
		$(top_srcdir)/src/gami.c \
		$(gamisubinclude_HEADERS)

girdir = $(INTROSPECTION_GIRDIR)
gir_DATA = Gami-1.0.gir

typelibdir = $(INTROSPECTION_TYPELIBDIR)
typelib_DATA = Gami-1.0.typelib

EXTRA_DIST += $(gir_DATA)

%.typelib: %.gir $(INTROSPECTION_COMPILER)
	$(AM_V_GEN) \
	LD_LIBRARY_PATH=.libs$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH} \
	$(INTROSPECTION_COMPILER) \
		--includedir=$(srcdir) \
		--includedir=.  \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

DISTCLEANFILES += $(gir_DATA) $(typelib_DATA)

endif # HAVE_INTROSPECTION
