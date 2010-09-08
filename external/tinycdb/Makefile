LIB=	cdb
INTERNALLIB=
SRCS= cdb_init.c cdb_find.c cdb_findnext.c cdb_seq.c cdb_seek.c \
      cdb_unpack.c \
      cdb_make_add.c cdb_make_put.c cdb_make.c cdb_hash.c

CFLAGS+=-fPIC
WARNS?=3
WFORMAT?=1

NO_MAN=true

.include <bsd.lib.mk>
