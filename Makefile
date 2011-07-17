SUBDIR=	external \
	libpkg \
	pkg \
	pkg2legacy

CFLAGS=	-pg -O2 -pipe -mtune=native -std=c99 -I/home/eitan/svn/pkgng/libpkg  -I/home/eitan/svn/pkgng/libpkg/../external/sqlite  -I/home/eitan/svn/pkgng/libpkg/../external/libyaml/include -g -O0 -DDEBUG -std=gnu99 -fstack-protector -Wsystem-headers -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wunused-parameter -Wchar-subscripts -Winline -Wnested-externs -Wredundant-decls -c pkg_event.c -o pkg_event.po
.include <bsd.subdir.mk>
