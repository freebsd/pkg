noinst_HEADERS=	blake2.h \
				blake2-impl.h

noinst_LTLIBRARIES=	libblake2.la libblake2_static.la

blake2_common_cflags=	-I$(top_srcdir)/compat -O3
libblake2_la_SOURCES=	blake2b-ref.c
libblake2_la_CFLAGS=	$(blake2_common_cflags) -shared
libblake2_static_la_SOURCES=	$(libblake2_la_SOURCES)
libblake2_static_la_CFLAGS=	$(blake2_common_cflags) -static
libblake2_static_la_LDFLAGS=	-all-static