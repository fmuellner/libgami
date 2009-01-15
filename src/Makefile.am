INCLUDES =                                         \
	   -DG_LOG_DOMAIN=\"Gami\"                 \
	   -DGAMI_COMPILATION                      \
	   $(GAMI_CFLAGS)

gamiincludedir=$(includedir)/gami-1.0
gamiinclude_HEADERS = gami.h

gamisubincludedir=$(gamiincludedir)/gami
gamisubinclude_HEADERS = gami-main.h gami-manager.h gami-response.h

lib_LTLIBRARIES = \
	libgami-1.0.la

libgami_1_0_la_SOURCES = \
	gami-main.c      \
	gami-response.c  \
	gami-manager.c

AM_CPPFLAGS = -Wall

libgami_1_0_la_LIBADD = \
	$(GAMI_LIBS)
