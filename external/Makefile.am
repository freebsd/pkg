AUTOMAKE_OPTIONS=	subdir-objects

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
			libelf/_elftc.h \
			libelf/_libelf.h \
			libelf/_libelf_ar.h \
			libelf/_libelf_config.h \
			libelf/elfdefinitions.h \
			libelf/gelf.h \
			libelf/libelf.h \
			libelf/native-elf-format.h \
			libucl/include/ucl.h \
			libucl/src/xxhash.h \
			libucl/src/ucl_internal.h \
			libucl/src/ucl_hash.h \
			libucl/src/ucl_chartable.h \
			libucl/uthash/uthash.h \
			libucl/uthash/utlist.h \
			libucl/uthash/utstring.h \
			libyaml/config.h \
			libyaml/include/yaml.h \
			libyaml/src/yaml_private.h \
			libyaml/win32/config.h \
			sqlite/sqlite3.h \
			uthash/uthash.h \
			uthash/utlist.h

EXTRA_LTLIBRARIES=	libelf.la

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
			
noinst_LTLIBRARIES=	libucl.la libsqlite.la libyaml.la libexpat.la @LIBELF_BUNDLED@

libucl_la_SOURCES=	libucl/src/ucl_emitter.c \
			libucl/src/ucl_hash.c \
			libucl/src/ucl_parser.c \
			libucl/src/ucl_util.c \
			libucl/src/xxhash.c
libucl_la_CFLAGS=	-I$(top_srcdir)/external//libucl/uthash \
			-I$(top_srcdir)/external/libucl/include  \
			-I$(top_srcdir)/external//libucl/src \
			-Wno-unused-parameter -Wno-pointer-sign
libexpat_la_SOURCES=	expat/lib/xmlparse.c \
			expat/lib/xmlrole.c \
			expat/lib/xmltok.c
libexpat_la_CFLAGS=	-I$(top_srcdir)/external/expat \
			-DHAVE_EXPAT_CONFIG_H
libyaml_la_SOURCES=	libyaml/src/api.c \
			libyaml/src/loader.c \
			libyaml/src/parser.c \
			libyaml/src/reader.c \
			libyaml/src/scanner.c
libyaml_la_CFLAGS=	-I$(top_srcdir)/external/libyaml/include \
			-I$(top_srcdir)/external/libyaml \
			-Wno-strict-aliasing \
			-Wno-unused
libsqlite_la_SOURCES=	sqlite/sqlite3.c \
			sqlite/shell.c
libsqlite_la_CFLAGS=	-DHAVE_POSIX_FALLOCATE=1 \
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
			-Wno-strict-aliasing \
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
			-DSQLITE_THREADSAFE=1 \
			-DSQLITE_TEMP_STORE=3 \
			-Dmain=sqlite3_shell \
			-DNDEBUG

CLEANFILES=	libelf/libelf_fsize.c \
			libelf/libelf_msize.c \
			libelf/libelf_convert.c \
			libelf/native-elf-format.h
BUILT_SOURCES = libelf/native-elf-format.h

libelf/libelf_convert.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=libelf libelf/elf_types.m4 libelf/libelf_convert.m4 > $@

libelf/libelf_fsize.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=libelf libelf/elf_types.m4 libelf/libelf_fsize.m4 > $@

libelf/libelf_msize.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=libelf libelf/elf_types.m4 libelf/libelf_msize.m4 > $@
libelf/native-elf-format.h:
	libelf/native-elf-format > libelf/native-elf-format.h