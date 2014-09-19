pkg_common_cflags=	-I$(top_srcdir)/libpkg \
			@LIBSBUF_INCLUDE@ \
			@LDNS_CFLAGS@ \
			-I$(top_srcdir)/external/expat/lib \
			-I$(top_srcdir)/external/libucl/include \
			-I$(top_srcdir)/external/uthash \
			-I$(top_srcdir)/external/sqlite \
			-Wno-pointer-sign

librepo_binary_la_SOURCES=	binary.c \
							common.c \
							fetch.c \
							init.c \
							query.c \
							update.c
librepo_binary_la_CFLAGS=	$(pkg_common_cflags) -shared

librepo_binary_static_la_LDFLAGS=	-all-static
librepo_binary_static_la_SOURCES=	$(librepo_binary_la_SOURCES)
librepo_binary_static_la_CFLAGS=	$(pkg_common_cflags) -static

noinst_LTLIBRARIES= librepo_binary.la librepo_binary_static.la
noinst_HEADERS=		binary.h binary_private.h
