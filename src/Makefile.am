INCLUDES = -DG_LOG_DOMAIN=\"Gami\"  \
	   -DGAMI_COMPILATION       \
	   $(GAMI_CFLAGS)

gamiincludedir=$(includedir)/libgami-1.0
gamiinclude_HEADERS = gami.h

gamisubincludedir=$(gamiincludedir)/gami
gamisubinclude_HEADERS = gami-main.h gami-manager.h gami-response.h

lib_LTLIBRARIES = libgami-1.0.la

libgami_1_0_la_SOURCES = gami-main.c      \
			 gami-response.c  \
			 gami-manager.c

AM_CPPFLAGS = -Wall

libgami_1_0_la_LDFLAGS = $(GAMI_LIBS)

BUILT_GIRSOURCES = GAmi-1.0.gir

if HAVE_INTROSPECTION

GAmi-1.0.gir: $(libgami_1_0_la_SOURCES) libgami-1.0.la $(G_IR_SCANNER)
	$(G_IR_SCANNER) -v --namespace GAmi --nsversion=1.0 \
		--add-include-path=$(top_srcdir)/src --add-include-path=. \
		--include=GObject-2.0 \
		--library=gami-1.0 \
		--libtool="$(LIBTOOL)" \
		--output $@ \
		--pkg gobject-2.0 \
		-I$(top_srcdir)/src \
		$(GAMI_CFLAGS) \
		-DGAMI_COMPILATION \
		$(gamisubinclude_HEADERS:%=$(top_srcdir)/src/%) \
		$(libgami_1_0_la_SOURCES:%=$(top_srcdir)/src/%)

girdir = $(GIRDIR)
gir_DATA = $(BUILT_GIRSOURCES)

typelibdir = $(TYPELIBDIR)
typelib_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)

EXTRA_DIST = $(gir_DATA) $(typelib_DATA)
DISTCLEANFILES = $(gir_DATA) $(typelib_DATA)

%.typelib: %.gir $(G_IR_COMPILER)
	LD_LIBRARY_PATH=$${LD_LIBRARY_PATH:+$$LD_LIBRARY_PATH:}.libs \
			$(G_IR_COMPILER) --includedir=$(srcdir) --includedir=. \
			$(G_IR_COMPILER_OPTS) $< -o $(builddir)/$(@F)

endif # HAVE_INTROSPECTION
