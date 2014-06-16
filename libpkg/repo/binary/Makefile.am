librepo_binary_la_SOURCES= dummy.c
librepo_binary_la_CFLAGS=	$(pkg_common_cflags) -shared

librepo_binary_static_la_LDFLAGS=	-all-static
librepo_binary_static_la_SOURCES=	$(librepo_binary_la_SOURCES)
librepo_binary_static_la_CFLAGS=	$(pkg_common_cflags) -static

noinst_LTLIBRARIES= librepo_binary.la librepo_binary_static.la