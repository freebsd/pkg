.include <bsd.own.mk>

LIB=		pkg
INCS=		pkg.h
# That's cheating real warnings are set later
WARNS=		0
PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
SHLIB_MAJOR=	1

MANDIR=		${PREFIX}/man/man

EXPATSRCS=	${.CURDIR}/../external/expat/lib/xmlparse.c \
		${.CURDIR}/../external/expat/lib/xmlrole.c \
		${.CURDIR}/../external/expat/lib/xmltok.c
UCLSRCS=	${.CURDIR}/../external/libucl/src/ucl_emitter.c \
		${.CURDIR}/../external/libucl/src/ucl_hash.c \
		${.CURDIR}/../external/libucl/src/ucl_parser.c \
		${.CURDIR}/../external/libucl/src/ucl_util.c \
		${.CURDIR}/../external/libucl/src/xxhash.c
SQLITESRCS=	${.CURDIR}/../external/sqlite/sqlite3.c \
		${.CURDIR}/../external/sqlite/shell.c
YAMLSRCS=	${.CURDIR}/../external/libyaml/src/api.c \
		${.CURDIR}/../external/libyaml/src/loader.c \
		${.CURDIR}/../external/libyaml/src/parser.c \
		${.CURDIR}/../external/libyaml/src/reader.c \
		${.CURDIR}/../external/libyaml/src/scanner.c
ELFSRCS+=	${.CURDIR}/../external/libelf/elf.c \
		${.CURDIR}/../external/libelf/elf_begin.c \
		${.CURDIR}/../external/libelf/elf_cntl.c \
		${.CURDIR}/../external/libelf/elf_end.c \
		${.CURDIR}/../external/libelf/elf_errmsg.c \
		${.CURDIR}/../external/libelf/elf_errno.c \
		${.CURDIR}/../external/libelf/elf_data.c \
		${.CURDIR}/../external/libelf/elf_fill.c \
		${.CURDIR}/../external/libelf/elf_flag.c \
		${.CURDIR}/../external/libelf/elf_getarhdr.c \
		${.CURDIR}/../external/libelf/elf_getarsym.c \
		${.CURDIR}/../external/libelf/elf_getbase.c \
		${.CURDIR}/../external/libelf/elf_getident.c \
		${.CURDIR}/../external/libelf/elf_hash.c \
		${.CURDIR}/../external/libelf/elf_kind.c \
		${.CURDIR}/../external/libelf/elf_memory.c \
		${.CURDIR}/../external/libelf/elf_next.c \
		${.CURDIR}/../external/libelf/elf_open.c \
		${.CURDIR}/../external/libelf/elf_rand.c \
		${.CURDIR}/../external/libelf/elf_rawfile.c \
		${.CURDIR}/../external/libelf/elf_phnum.c \
		${.CURDIR}/../external/libelf/elf_shnum.c \
		${.CURDIR}/../external/libelf/elf_shstrndx.c \
		${.CURDIR}/../external/libelf/elf_scn.c \
		${.CURDIR}/../external/libelf/elf_strptr.c \
		${.CURDIR}/../external/libelf/elf_update.c \
		${.CURDIR}/../external/libelf/elf_version.c \
		${.CURDIR}/../external/libelf/gelf_cap.c \
		${.CURDIR}/../external/libelf/gelf_checksum.c \
		${.CURDIR}/../external/libelf/gelf_dyn.c \
		${.CURDIR}/../external/libelf/gelf_ehdr.c \
		${.CURDIR}/../external/libelf/gelf_getclass.c \
		${.CURDIR}/../external/libelf/gelf_fsize.c \
		${.CURDIR}/../external/libelf/gelf_move.c \
		${.CURDIR}/../external/libelf/gelf_phdr.c \
		${.CURDIR}/../external/libelf/gelf_rel.c \
		${.CURDIR}/../external/libelf/gelf_rela.c \
		${.CURDIR}/../external/libelf/gelf_shdr.c \
		${.CURDIR}/../external/libelf/gelf_sym.c \
		${.CURDIR}/../external/libelf/gelf_syminfo.c \
		${.CURDIR}/../external/libelf/gelf_symshndx.c \
		${.CURDIR}/../external/libelf/gelf_xlate.c \
		${.CURDIR}/../external/libelf/libelf_align.c \
		${.CURDIR}/../external/libelf/libelf_allocate.c \
		${.CURDIR}/../external/libelf/libelf_ar.c \
		${.CURDIR}/../external/libelf/libelf_ar_util.c \
		${.CURDIR}/../external/libelf/libelf_checksum.c \
		${.CURDIR}/../external/libelf/libelf_data.c \
		${.CURDIR}/../external/libelf/libelf_ehdr.c \
		${.CURDIR}/../external/libelf/libelf_extended.c \
		${.CURDIR}/../external/libelf/libelf_memory.c \
		${.CURDIR}/../external/libelf/libelf_open.c \
		${.CURDIR}/../external/libelf/libelf_phdr.c \
		${.CURDIR}/../external/libelf/libelf_shdr.c \
		${.CURDIR}/../external/libelf/libelf_xlate.c \
		${GENSRCS}
GENSRCS=	libelf_fsize.c libelf_msize.c libelf_convert.c
CLEANFILES=	${GENSRCS}

libelf_convert.c:	${.CURDIR}/../external/libelf/elf_types.m4 ${.CURDIR}/../external/libelf/libelf_convert.m4
libelf_fsize.c:		${.CURDIR}/../external/libelf/elf_types.m4 ${.CURDIR}/../external/libelf/libelf_fsize.m4
libelf_msize.c:		${.CURDIR}/../external/libelf/elf_types.m4 ${.CURDIR}/../external/libelf/libelf_msize.m4

PC=		pkg.pc
PKGH=		pkg.h
LIBPKGSRCS=	${PC} \
		${PKGH} \
		backup.c \
		dns_utils.c \
		elfhints.c \
		fetch.c \
		packing.c \
		pkg.c \
		pkg_add.c \
		pkg_attributes.c \
		pkg_config.c \
		pkg_create.c \
		pkg_delete.c \
		pkg_elf.c \
		pkg_event.c \
		pkg_jobs.c \
		pkg_manifest.c \
		pkg_ports.c \
		pkg_printf.c \
		pkg_repo.c \
		pkg_status.c \
		pkg_version.c \
		pkgdb.c \
		pkgdb_repo.c \
		rcscripts.c \
		rsa.c \
		ssh.c \
		scripts.c \
		update.c \
		utils.c \
		plugins.c \
		pkg_old.c \
		pkg.pc

MAN=		pkg_printf.3 \
		pkg_repos.3 \
		pkg-repository.5

CLEANFILES=	${PC} ${PKGH}

NEWVERS=	${.CURDIR}/../newvers.sh

PKGVERSION!=	sh ${NEWVERS} pkg
PORTVERSION!=	sh ${NEWVERS} port

.if defined(CREATE_SNAPSHOT)
_snapshot=	snapshot

${_snapshot}:
.endif

all: ${PC}

realinstall: ${PC}
	${INSTALL} -m 644 ${PC} ${DESTDIR}${PREFIX}/libdata/pkgconfig

#
# Set DEFAULT_MIRROR_TYPE default to SRV
# Possible values are:
# 0: none
# 1: srv
# 2: http
#
DEFAULT_MIRROR_TYPE?=	1

CCV!=	${CC} --version
.if ${CCV:M*clang*}
USING_CLANG=	yes
.endif

EXPATCFLAGS=	-I${.CURDIR}/../external/expat/ \
		-DHAVE_EXPAT_CONFIG_H

.if defined(USING_CLANG)
EXPATCFLAGS+=	-Wno-enum-conversion
.endif

UCLCFLAGS=	-I${.CURDIR}/../external/libucl/include \
		-I${.CURDIR}/../external/libucl/src \
		-I${.CURDIR}/../external/libucl/uthash \
		-Wno-pointer-sign \
		-Wno-strict-aliasing \
		-Wno-unused-function

OSNAME!=	uname -s
.if ${OSNAME} == FreeBSD
SQLITECFLAGS+=	-DHAVE_POSIX_FALLOCATE=1
.endif
SQLITECFLAGS+=	-DSTDC_HEADERS=1 \
		-DHAVE_SYS_TYPES_H=1 \
		-DHAVE_SYS_STAT_H=1 \
		-DHAVE_STDLIB_H=1 \
		-DHAVE_STRING_H=1 \
		-DHAVE_MEMORY_H=1 \
		-DHAVE_STRINGS_H=1 \
		-DHAVE_INTTYPES_H=1 \
		-DHAVE_STDINT_H=1 \
		-DHAVE_UNISTD_H=1 \
		-DHAVE_DLFCN_H=1 \
		-DHAVE_USLEEP=1 \
		-DHAVE_LOCALTIME_R=1 \
		-DHAVE_GMTIME_R=1 \
		-DHAVE_DECL_STRERROR_R=1 \
		-DHAVE_STRERROR_R=1 \
		-Wno-unused-variable \
		-Wno-unused-function \
		-Wno-strict-aliasing
# http://www.sqlite.org/compile.html
SQLITECFLAGS+=	-DSQLITE_OMIT_AUTOVACUUM \
		-DSQLITE_OMIT_BLOB_LITERAL \
		-DSQLITE_OMIT_DECLTYPE \
		-DSQLITE_OMIT_EXPLAIN \
		-DSQLITE_OMIT_DEPRECATED \
		-DSQLITE_OMIT_LOAD_EXTENSION \
		-DSQLITE_OMIT_PROGRESS_CALLBACK \
		-DSQLITE_OMIT_TCL_VARIABLE \
		-DSQLITE_OMIT_UTF16 \
		-DSQLITE_OMIT_CAT \
		-DSQLITE_OMIT_CHECK \
		-DSQLITE_OMIT_AUTOINIT \
		-DSQLITE_OMIT_COMPILEOPTION_DIAGS \
		-DSQLITE_OMIT_INTEGRITY_CHECK \
		-DSQLITE_OMIT_BUILTIN_TEST \
		-DSQLITE_OMIT_SHARED_CACHE \
		-DSQLITE_ENABLE_UNLOCK_NOTIFY=1 \
		-DUSE_PREAD \
		-DSQLITE_THREADSAFE=1 \
		-DSQLITE_TEMP_STORE=3 \
		-Dmain=sqlite3_shell \
		-DNDEBUG
YAMLCFLAGS=	-I${.CURDIR}/../external/libyaml/include \
		-I${.CURDIR}/../external/libyaml \
		-DHAVE_CONFIG_H \
		-Wno-strict-aliasing \
		-Wno-unused
LIBPKGCFLAGS+=	-DDEFAULT_MIRROR_TYPE=${DEFAULT_MIRROR_TYPE} \
		-std=gnu99 \
		-I. \
		-I${.CURDIR} \
		-I${.CURDIR}/../external/sqlite \
		-I${.CURDIR}/../external/uthash \
		-I${.CURDIR}/../external/libucl/include \
		-I${.CURDIR}/../external/libyaml/include \
		-DPREFIX=\"${PREFIX}\" \
		-Wno-pointer-sign
.if !exists(/usr/lib/libelf.so)
LIBPKGCFLAGS+=	-I${.CURDIR}/../external/libelf \
		-DBUNDLED_LIBELF
ELFCFLAGS+=	-I${.CURDIR}/../external/libelf
SRCS+=		${ELFSRCS}
.endif

.if defined(PKG_PORTSDIR)
LIBPKGCFLAGS+=	-DPORTSDIR=\"${PKG_PORTSDIR}\"
.endif

SRCS+=	${UCLSRCS} ${EXPATSRCS} ${SQLITESRCS} ${YAMLSRCS} ${LIBPKGSRCS}

.for c in ${UCLSRCS}
CFLAGS.${c}+=	${UCLCFLAGS}
.endfor

.for c in ${EXPATSRCS}  
CFLAGS.${c}+=	${EXPATCFLAGS}
.endfor

.for c in ${SQLITESRCS}
CFLAGS.${c}+=	${SQLITECFLAGS}
.endfor

.for c in ${YAMLSRCS}
CFLAGS.${c}+=	${YAMLCFLAGS}
.endfor

.for c in ${LIBPKGSRCS}
CFLAGS.${c}+=	${LIBPKGCFLAGS}
.endfor

.for c in ${ELFSRCS}
CFLAGS.${c}+=	${ELFCFLAGS}
.endfor


.if ${.TARGETS} == "depend"
CFLAGS+=	${UCLCFLAGS}  ${EXPATCFLAGS} ${SQLITECFLAGS} ${YAMLCFLAGS} ${LIBPKGCFLAGS}
.endif

CFLAGS+=	-Wall -Werror
CFLAGS+=	${CFLAGS.${.IMPSRC}}

LDADD+=		-L../external/sqlite \
		-L../external/libyaml \
		-L../external/libucl

.if exists(/usr/lib/libelf.so)
LDADD+=		-lelf
.endif

LDADD+=		-larchive \
		-lsbuf \
		-lfetch \
		-lutil \
		-lpthread

.if exists(/usr/include/edit/readline/readline.h)
LDADD+=		-ledit
.endif

.if defined(DEBUG_FLAGS)
.if ${DEBUG_FLAGS} == 1
DEBUG_FLAGS=	-ggdb -O0
.else
DEBUG_FLAGS+=	-ggdb -O0
.endif
.endif
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif

.include <bsd.lib.mk>

.SUFFIXES: .pc.in .pc .h.in .h .m4 .c

.PHONY:	${_snapshot}

.h.in.h: ${NEWVERS} ${_snapshot}
	sed -e 's,%%PKGVERSION%%,$(PKGVERSION),'   \
	    -e 's,%%PORTVERSION%%,$(PORTVERSION),' \
	    ${.IMPSRC} > ${.TARGET}

.pc.in.pc: ${NEWVERS} ${_snapshot}
	sed -e "s,__PREFIX__,${PREFIX},g" \
	    -e "s,__VERSION__,${PKGVERSION},g" \
	    ${.IMPSRC} > ${.TARGET}

.m4.c:
	(cd ${.CURDIR}/../external/libelf ; m4 -D SRCDIR=${.CURDIR}/../external/libelf ${.IMPSRC}) > ${.TARGET}

# Workaround 8.3/8.4 bug
OSVERSION!=    /sbin/sysctl -n kern.osreldate

.if ${OSNAME} == FreeBSD && ${OSVERSION} < 901000
.c.o:
	${CC} ${STATIC_CFLAGS} ${CFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endif
