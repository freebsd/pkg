AUTOMAKE_OPTIONS=	subdir-objects

dist_check_SCRIPTS=	libelf/native-elf-format \
			libelf/elf_types.m4 \
			libelf/libelf_convert.m4 \
			libelf/libelf_fsize.m4 \
			libelf/libelf_msize.m4
noinst_HEADERS= expat/amiga/expat_68k.h \
		expat/amiga/expat_base.h \
		expat/amiga/include/inline4/expat.h \
		expat/amiga/include/interfaces/expat.h \
		expat/amiga/include/libraries/expat.h \
		expat/amiga/include/proto/expat.h \
		expat/expat_config.h \
		expat/lib/amigaconfig.h \
		expat/lib/ascii.h \
		expat/lib/asciitab.h \
		expat/lib/expat.h \
		expat/lib/expat_external.h \
		expat/lib/iasciitab.h \
		expat/lib/internal.h \
		expat/lib/latin1tab.h \
		expat/lib/macconfig.h \
		expat/lib/nametab.h \
		expat/lib/utf8tab.h \
		expat/lib/winconfig.h \
		expat/lib/xmlrole.h \
		expat/lib/xmltok.h \
		expat/lib/xmltok_impl.h \
		expat/tests/chardata.h \
		expat/tests/minicheck.h \
		expat/vms/expat_config.h \
		expat/xmlwf/codepage.h \
		expat/xmlwf/filemap.h \
		expat/xmlwf/xmlfile.h \
		expat/xmlwf/xmlmime.h \
		expat/xmlwf/xmltchar.h \
		expat/xmlwf/xmlurl.h \
		expat/lib/xmltok_impl.c \
		expat/lib/xmltok_ns.c \
		libelf/_elftc.h \
		libelf/_libelf.h \
		libelf/_libelf_ar.h \
		libelf/_libelf_config.h \
		libelf/elfdefinitions.h \
		libelf/gelf.h \
		libelf/libelf.h \
		libmachista/hashmap.h \
		libmachista/libmachista.h \
		libsbuf/sys/sbuf.h \
		libucl/klib/kvec.h \
		libucl/klib/khash.h \
		libucl/include/ucl.h \
		libucl/src/xxhash.h \
		libucl/src/ucl_internal.h \
		libucl/src/ucl_hash.h \
		libucl/src/ucl_chartable.h \
		libucl/src/tree.h \
		libucl/uthash/uthash.h \
		libucl/uthash/utlist.h \
		libucl/uthash/utstring.h \
		picosat/picosat.h \
		sqlite/sqlite3.h \
		uthash/utarray.h \
		uthash/uthash.h \
		uthash/utlist.h \
		include/tree.h \
		include/siphash.h \
		config.h \
		libfetch/common.h \
		libfetch/fetch.h \
		libfetch/httperr.h \
		libfetch/ftperr.h

DYNLIBS=		libucl.la \
			libsqlite.la \
			libexpat.la \
			libsbuf.la \
			libpicosat.la \
			libfetch.la
noinst_LTLIBRARIES=	libucl_static.la \
			libsqlite_static.la \
			libexpat_static.la \
			libsbuf_static.la \
			libpicosat_static.la \
			libfetch_static.la
if LIBELF_BUNDLED
DYNLIBS+=		libelf.la
noinst_LTLIBRARIES+=	libelf_static.la
endif

if HAVE_MACHO_ABI
noinst_LTLIBRARIES+=	libmachista_static.la
endif

if DYNAMIC
noinst_LTLIBRARIES+=	$(DYNLIBS)
endif

libelf_la_SOURCES=	libelf/elf.c \
			libelf/elf_begin.c \
			libelf/elf_cntl.c \
			libelf/elf_data.c \
			libelf/elf_end.c \
			libelf/elf_errmsg.c \
			libelf/elf_errno.c \
			libelf/elf_fill.c \
			libelf/elf_flag.c \
			libelf/elf_getarhdr.c \
			libelf/elf_getarsym.c \
			libelf/elf_getbase.c \
			libelf/elf_getident.c \
			libelf/elf_hash.c \
			libelf/elf_kind.c \
			libelf/elf_memory.c \
			libelf/elf_next.c \
			libelf/elf_open.c \
			libelf/elf_phnum.c \
			libelf/elf_rand.c \
			libelf/elf_rawfile.c \
			libelf/elf_scn.c \
			libelf/elf_shnum.c \
			libelf/elf_shstrndx.c \
			libelf/elf_strptr.c \
			libelf/elf_update.c \
			libelf/elf_version.c \
			libelf/gelf_cap.c \
			libelf/gelf_checksum.c \
			libelf/gelf_dyn.c \
			libelf/gelf_ehdr.c \
			libelf/gelf_fsize.c \
			libelf/gelf_getclass.c \
			libelf/gelf_move.c \
			libelf/gelf_phdr.c \
			libelf/gelf_rel.c \
			libelf/gelf_rela.c \
			libelf/gelf_shdr.c \
			libelf/gelf_sym.c \
			libelf/gelf_syminfo.c \
			libelf/gelf_symshndx.c \
			libelf/gelf_xlate.c \
			libelf/libelf_align.c \
			libelf/libelf_allocate.c \
			libelf/libelf_ar.c \
			libelf/libelf_ar_util.c \
			libelf/libelf_checksum.c \
			libelf/libelf_data.c \
			libelf/libelf_ehdr.c \
			libelf/libelf_extended.c \
			libelf/libelf_memory.c \
			libelf/libelf_open.c \
			libelf/libelf_phdr.c \
			libelf/libelf_shdr.c \
			libelf/libelf_xlate.c \
			libelf/libelf_fsize.c \
			libelf/libelf_msize.c \
			libelf/libelf_convert.c
libelf_la_CFLAGS=	-I$(top_srcdir)/external/libelf
libelf_static_la_SOURCES=	${libelf_la_SOURCES}
libelf_static_la_CFLAGS=	$(libelf_la_CFLAGS) -static
libelf_static_la_LDFLAGS=	-all-static

libmachista_static_la_SOURCES=	libmachista/libmachista.c \
				libmachista/hashmap.c
libmachista_static_la_CFLAGS=	-I$(tip_srcdir)/external/libmachista -static
libmachista_static_la_LDFLAGS=	-all-static

libsbuf_la_SOURCES=	libsbuf/subr_sbuf.c
sbuf_common_CFLAGS=	-I$(top_srcdir)/external/libsbuf
libsbuf_la_CFLAGS=	$(sbuf_common_CFLAGS) -shared
libsbuf_static_la_SOURCES=	${libsbuf_la_SOURCES}
libsbuf_static_la_CFLAGS=	$(sbuf_common_CFLAGS) -static
libsbuf_static_la_LDFLAGS=	-all-static

ucl_common_cflags=	-I$(top_srcdir)/external/libucl/uthash \
			-I$(top_srcdir)/external/libucl/klib \
			-I$(top_srcdir)/external/libucl/include  \
			-I$(top_srcdir)/external/libucl/src \
			-Wno-unused-parameter -Wno-pointer-sign
libucl_la_SOURCES=	libucl/src/ucl_emitter.c \
			libucl/src/ucl_emitter_utils.c \
			libucl/src/ucl_emitter_streamline.c \
			libucl/src/ucl_hash.c \
			libucl/src/ucl_parser.c \
			libucl/src/ucl_schema.c \
			libucl/src/ucl_util.c \
			libucl/src/xxhash.c
libucl_la_CFLAGS=	$(ucl_common_cflags) -shared
libucl_static_la_SOURCES=	$(libucl_la_SOURCES)
libucl_static_la_CFLAGS=	$(ucl_common_cflags) -static
libucl_static_la_LDFLAGS=	-all-static

expat_common_cflags=	-I$(top_srcdir)/external/expat \
			-I$(top_srcdir)/external/expat/lib \
			-DHAVE_EXPAT_CONFIG_H
libexpat_la_SOURCES=	expat/lib/xmlparse.c \
			expat/lib/xmlrole.c \
			expat/lib/xmltok.c
libexpat_la_CFLAGS=	$(expat_common_cflags) -shared
libexpat_static_la_SOURCES=	$(libexpat_la_SOURCES)
libexpat_static_la_CFLAGS=	$(expat_common_cflags) -static
libexpat_static_la_LDFLAGS=	-all-static

picosat_common_cflags=	-I$(top_srcdir)/external/picosat \
			-Wno-strict-aliasing \
			-Wno-unused	
libpicosat_la_SOURCES=	picosat/picosat.c \
			picosat/version.c
libpicosat_la_CFLAGS=	$(picosat_common_cflags) -shared
libpicosat_static_la_SOURCES=	$(libpicosat_la_SOURCES)
libpicosat_static_la_CFLAGS= $(picosat_common_cflags) -static
libpicosat_static_la_LDFLAGS=	-all-static

sqlite_common_cflags=	-DHAVE_SQLITE_CONFIG_H \
			-Wno-unused-variable \
			-Wno-unused-function \
			-Wno-strict-aliasing \
			-DHAVE_USLEEP=1 \
			-DSQLITE_OMIT_AUTOVACUUM \
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
			-DSQLITE_THREADSAFE=0 \
			-DSQLITE_TEMP_STORE=3 \
			-DSQLITE_ENABLE_FTS4 \
			-DSQLITE_SHELL_DBNAME_PROC=pkgshell_open \
			-DNDEBUG
libsqlite_la_SOURCES=	sqlite/sqlite3.c \
			sqlite/shell.c
libsqlite_la_CFLAGS=	$(sqlite_common_cflags) -shared
libsqlite_static_la_SOURCES=	$(libsqlite_la_SOURCES)
libsqlite_static_la_CFLAGS=	$(sqlite_common_cflags) -static
libsqlite_static_la_LDFLAGS=	-all-static

libfetch_common_cflags=	-I$(top_srcdir)/compat -DWITH_SSL -Wno-pointer-sign
libfetch_la_CFLAGS=	$(libfetch_common_cflags) -shared
libfetch_la_SOURCES=	libfetch/common.c \
			libfetch/fetch.c \
			libfetch/file.c \
			libfetch/ftp.c \
			libfetch/http.c
libfetch_static_la_CFLAGS=	$(libfetch_common_cflags) -static
libfetch_static_la_SOURCES=	${libfetch_la_SOURCES}

if HAVE_ELF_ABI
CLEANFILES=	libelf/libelf_fsize.c \
			libelf/libelf_msize.c \
			libelf/libelf_convert.c \
			libelf/native-elf-format.h
BUILT_SOURCES = libelf/native-elf-format.h \
		libelf/libelf_convert.c \
		libelf/libelf_fsize.c \
		libelf/libelf_msize.c

libelf/libelf_convert.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=$(srcdir)/libelf $(srcdir)/libelf/elf_types.m4 $(srcdir)/libelf/libelf_convert.m4 > $@

libelf/libelf_fsize.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=$(srcdir)/libelf $(srcdir)/libelf/elf_types.m4 $(srcdir)/libelf/libelf_fsize.m4 > $@

libelf/libelf_msize.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=$(srcdir)/libelf $(srcdir)/libelf/elf_types.m4 $(srcdir)/libelf/libelf_msize.m4 > $@
libelf/native-elf-format.h:
	$(srcdir)/libelf/native-elf-format > libelf/native-elf-format.h

endif

SUBDIRS=	blake2