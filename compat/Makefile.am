noinst_LTLIBRARIES=	libbsd_compat.la
noinst_HEADERS=		bsd_compat.h endian_util.h gr_util.h \
			humanize_number.h

libbsd_compat_la_SOURCES=	basename.c \
				closefrom.c \
				dirname.c \
				file_at.c \
				gr_util.c \
				humanize_number.c \
				strtonum.c \
				strnstr.c \
				funopen.c

libbsd_compat_la_CFLAGS=	$(pkg_common_cflags) -shared
