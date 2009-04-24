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
        gami-main.c               \
        gami-main.h               \
        gami-manager.c            \
        gami-manager.h            \
        gami-manager-private.c    \
        gami-manager-private.h    \
        gami-enums.h              \
        gami-enumtypes.c          \
        gami-enumtypes.h          \
	gami-error.c              \
	gami-error.h              \
        $(NULL)

libgami_1_0_la_LDFLAGS = $(GAMI_LIBS)

gamiincludedir=$(includedir)/libgami-1.0
gamiinclude_HEADERS = gami.h

gami_headers =                    \
	gami-main.h               \
	gami-manager.h            \
	gami-enums.h              \
	gami-error.h              \
	$(NULL)

gamisubincludedir=$(gamiincludedir)/gami
gamisubinclude_HEADERS =          \
	$(gami_headers)           \
	gami-enumtypes.h          \
	$(NULL)


BUILT_SOURCES =                   \
        gami-enumtypes.h          \
        gami-enumtypes.c          \
        $(NULL)

EXTRA_DIST +=                     \
        gami-enumtypes.h.template \
        gami-enumtypes.c.template \
        $(NULL)

gami-enumtypes.h: gami-enumtypes.h.template $(gami_headers)
	glib-mkenums --template $< $(gami_headers) >$@.tmp && mv $@.tmp $@

gami-enumtypes.c: gami-enumtypes.c.template $(gami_headers)
	glib-mkenums --template $< $(gami_headers) >$@.tmp && mv $@.tmp $@

if ENABLE_INTROSPECTION

GAmi-1.0.gir: libgami-1.0.la Makefile gami.h
	$(G_IR_SCANNER) \
		--namespace GAmi --nsversion=1.0 \
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

girdir = $(GIRDIR)
gir_DATA = GAmi-1.0.gir

typelibdir = $(TYPELIBDIR)
typelib_DATA = GAmi-1.0.typelib

EXTRA_DIST += $(gir_DATA)
DISTCLEANFILES += $(gir_DATA) $(typelib_DATA)

%.typelib: %.gir $(G_IR_COMPILER)
	LD_LIBRARY_PATH=$${LD_LIBRARY_PATH:+$$LD_LIBRARY_PATH:}.libs \
			$(G_IR_COMPILER) --includedir=$(srcdir) --includedir=. \
			$(G_IR_COMPILER_OPTS) $< -o $(builddir)/$(@F)

endif # ENABLE_INTROSPECTION
