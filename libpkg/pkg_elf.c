/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#elif HAVE_ENDIAN_H
#include <endian.h>
#elif HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#endif
#include <sys/types.h>
#ifdef HAVE_SYS_ELF_COMMON_H
#include <sys/elf_common.h>
#endif
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <err.h>
#include <fcntl.h>
#include <gelf.h>
#include <libgen.h>
#ifdef HAVE_LINK_H
#include <link.h>
#endif
#include <paths.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_LIBELF
#include <libelf.h>
#endif

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "private/elf_tables.h"
#include "private/ldconfig.h"

#ifndef NT_ABI_TAG
#define NT_ABI_TAG 1
#endif

/* FFR: when we support installing a 32bit package on a 64bit host */
#define _PATH_ELF32_HINTS       "/var/run/ld-elf32.so.hints"

#define roundup2(x, y)	(((x)+((y)-1))&(~((y)-1))) /* if y is powers of two */

static int
filter_system_shlibs(const char *name, char *path, size_t pathlen)
{
	const char *shlib_path;

	shlib_path = shlib_list_find_by_name(name);
	if (shlib_path == NULL) {
		/* dynamic linker could not resolve */
		return (EPKG_FATAL);
	}

	/* match /lib, /lib32, /usr/lib and /usr/lib32 */
	if (strncmp(shlib_path, "/lib", 4) == 0 ||
	    strncmp(shlib_path, "/usr/lib", 7) == 0)
		return (EPKG_END); /* ignore libs from base */

	if (path != NULL)
		strncpy(path, shlib_path, pathlen);

	return (EPKG_OK);
} 

/* ARGSUSED */
static int
add_shlibs_to_pkg(__unused void *actdata, struct pkg *pkg, const char *fpath,
		  const char *name, bool is_shlib)
{
	const char *pkgname, *pkgversion;
	struct pkg_file *file = NULL;
	const char *filepath;

	switch(filter_system_shlibs(name, NULL, 0)) {
	case EPKG_OK:		/* A non-system library */
		pkg_addshlib_required(pkg, name);
		return (EPKG_OK);
	case EPKG_END:		/* A system library */
		return (EPKG_OK);
	default:
		/* Ignore link resolution errors if we're analysing a
		   shared library. */
		if (is_shlib)
			return (EPKG_OK);

		while (pkg_files(pkg, &file) == EPKG_OK) {
			filepath = pkg_file_path(file);
			if (strcmp(&filepath[strlen(filepath) - strlen(name)], name) == 0) {
				pkg_addshlib_required(pkg, name);
				return (EPKG_OK);
			}
		}

		pkg_get(pkg, PKG_NAME, &pkgname, PKG_VERSION, &pkgversion);
		warnx("(%s-%s) %s - shared library %s not found",
		      pkgname, pkgversion, fpath, name);

		return (EPKG_FATAL);
	}
}

static int
analyse_elf(struct pkg *pkg, const char *fpath,
	int (action)(void *, struct pkg *, const char *, const char *, bool),
	void *actdata)
{
	Elf *e = NULL;
	GElf_Ehdr elfhdr;
	Elf_Scn *scn = NULL;
	Elf_Scn *note = NULL;
	Elf_Scn *dynamic = NULL;
	GElf_Shdr shdr;
	Elf_Data *data;
	GElf_Dyn *dyn, dyn_mem;
	struct stat sb;
	int ret = EPKG_OK;

	size_t numdyn = 0;
	size_t sh_link = 0;
	size_t dynidx;
	const char *osname;
	const char *shlib;

	bool developer = false;
	bool is_shlib = false;

	developer = pkg_object_bool(pkg_config_get("DEVELOPER_MODE"));

	int fd;

	if (lstat(fpath, &sb) != 0)
		pkg_emit_errno("fstat() failed for", fpath);
	/* ignore empty files and non regular files */
	if (sb.st_size == 0 || !S_ISREG(sb.st_mode))
		return (EPKG_END); /* Empty file or sym-link: no results */

	if ((fd = open(fpath, O_RDONLY, 0)) < 0) {
		return (EPKG_FATAL);
	}

	if ((e = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("elf_begin() for %s failed: %s", fpath,
		    elf_errmsg(-1));
		goto cleanup;
	}

	if (elf_kind(e) != ELF_K_ELF) {
		/* Not an elf file: no results */
		ret = EPKG_END;
		goto cleanup;
	}

	if (developer)
		pkg->flags |= PKG_CONTAINS_ELF_OBJECTS;

	if (gelf_getehdr(e, &elfhdr) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("getehdr() failed: %s.", elf_errmsg(-1));
		goto cleanup;
	}

	/* Elf file has sections header */
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			ret = EPKG_FATAL;
			pkg_emit_error("getshdr() for %s failed: %s", fpath,
					elf_errmsg(-1));
			goto cleanup;
		}
		switch (shdr.sh_type) {
		case SHT_NOTE:
			if ((data = elf_getdata(scn, NULL)) == NULL) {
				ret = EPKG_END; /* Some error occurred, ignore this file */
				goto cleanup;
			}
			else if (data->d_buf != NULL) {
				Elf_Note *en = (Elf_Note *)data->d_buf;
				if (en->n_type == NT_ABI_TAG)
					note = scn;
			}
			break;
		case SHT_DYNAMIC:
			dynamic = scn;
			sh_link = shdr.sh_link;
			numdyn = shdr.sh_size / shdr.sh_entsize;
			break;
		}

		if (note != NULL && dynamic != NULL)
			break;
	}

	/*
	 * note == NULL usually means a shared object for use with dlopen(3)
	 * dynamic == NULL means not a dynamically linked elf
	 */
	if (dynamic == NULL) {
		ret = EPKG_END;
		goto cleanup; /* not a dynamically linked elf: no results */
	}

	if (note != NULL) {
		if ((data = elf_getdata(note, NULL)) == NULL) {
			ret = EPKG_END; /* Some error occurred, ignore this file */
			goto cleanup;
		}
		if (data->d_buf == NULL) {
			ret = EPKG_END; /* No osname available */
			goto cleanup;
		}
		osname = (const char *) data->d_buf + sizeof(Elf_Note);
		if (strncasecmp(osname, "freebsd", sizeof("freebsd")) != 0 &&
		    strncasecmp(osname, "dragonfly", sizeof("dragonfly")) != 0) {
			ret = EPKG_END;	/* Foreign (probably linux) ELF object */
			goto cleanup;
		}
	} else {
		if (elfhdr.e_ident[EI_OSABI] != ELFOSABI_FREEBSD) {
			ret = EPKG_END;
			goto cleanup;
		}
	}

	if ((data = elf_getdata(dynamic, NULL)) == NULL) {
		ret = EPKG_END; /* Some error occurred, ignore this file */
		goto cleanup;
	}

	/* First, scan through the data from the .dynamic section to
	   find any RPATH or RUNPATH settings.  These are colon
	   separated paths to prepend to the ld.so search paths from
	   the ELF hints file.  These always seem to come right after
	   the NEEDED shared library entries.

	   NEEDED entries should resolve to a filename for installed
	   executables, but need not resolve for installed shared
	   libraries -- additional info from the apps that link
	   against them would be required.  Shared libraries are
	   distinguished by a DT_SONAME tag */

	rpath_list_init();
	for (dynidx = 0; dynidx < numdyn; dynidx++) {
		if ((dyn = gelf_getdyn(data, dynidx, &dyn_mem)) == NULL) {
			ret = EPKG_FATAL;
			pkg_emit_error("getdyn() failed for %s: %s", fpath,
			    elf_errmsg(-1));
			goto cleanup;
		}

		if (dyn->d_tag == DT_SONAME) {
			is_shlib = true;

			/* The file being scanned is a shared library
			   *provided* by the package. Record this if
			   appropriate */

			pkg_addshlib_provided(pkg, elf_strptr(e, sh_link, dyn->d_un.d_val));
		}

		if (dyn->d_tag != DT_RPATH && dyn->d_tag != DT_RUNPATH)
			continue;
		
		shlib_list_from_rpath(elf_strptr(e, sh_link, dyn->d_un.d_val), 
				      dirname(fpath));
		break;
	}
	if (!is_shlib) {
		/*
		 * Some shared libraries have no SONAME, but we still want
		 * to manage them in provides list.
		 */
		if (elfhdr.e_type == ET_DYN) {
			is_shlib = true;
			pkg_addshlib_provided(pkg, basename(fpath));
		}
	}

	/* Now find all of the NEEDED shared libraries. */

	for (dynidx = 0; dynidx < numdyn; dynidx++) {
		if ((dyn = gelf_getdyn(data, dynidx, &dyn_mem)) == NULL) {
			ret = EPKG_FATAL;
			pkg_emit_error("getdyn() failed for %s: %s", fpath,
			    elf_errmsg(-1));
			goto cleanup;
		}

		if (dyn->d_tag != DT_NEEDED)
			continue;

		shlib = elf_strptr(e, sh_link, dyn->d_un.d_val);

		action(actdata, pkg, fpath, shlib, is_shlib);
	}

cleanup:
	rpath_list_free();

	if (e != NULL)
		elf_end(e);
	close(fd);

	return (ret);
}

static int
analyse_fpath(struct pkg *pkg, const char *fpath)
{
	const char *dot;

	dot = strrchr(fpath, '.');

	if (dot == NULL)	/* No extension */
		return (EPKG_OK);

	if (dot[1] == 'a' && dot[2] == '\0')
		pkg->flags |= PKG_CONTAINS_STATIC_LIBS;

	if ((dot[1] == 'l' && dot[2] == 'a' && dot[3] == '\0') ||
	    (dot[1] == 'h' && dot[2] == '\0'))
		pkg->flags |= PKG_CONTAINS_H_OR_LA;

	return (EPKG_OK);
}

int
pkg_analyse_files(struct pkgdb *db, struct pkg *pkg, const char *stage)
{
	struct pkg_file *file = NULL;
	int ret = EPKG_OK;
	char fpath[MAXPATHLEN];
	bool developer = false;

	developer = pkg_object_bool(pkg_config_get("DEVELOPER_MODE"));

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);

	shlib_list_init();

	ret = shlib_list_from_elf_hints(_PATH_ELF_HINTS);
	if (ret != EPKG_OK)
		goto cleanup;

	/* Assume no architecture dependence, for contradiction */
	if (developer)
		pkg->flags &= ~(PKG_CONTAINS_ELF_OBJECTS |
				PKG_CONTAINS_STATIC_LIBS |
				PKG_CONTAINS_H_OR_LA);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (stage != NULL)
			snprintf(fpath, sizeof(fpath), "%s/%s", stage, pkg_file_path(file));
		else
			strlcpy(fpath, pkg_file_path(file), sizeof(fpath));

		ret = analyse_elf(pkg, fpath, add_shlibs_to_pkg, db);
		if (developer) {
			if (ret != EPKG_OK && ret != EPKG_END)
				goto cleanup;
			analyse_fpath(pkg, fpath);
		}
	}

	ret = EPKG_OK;

cleanup:
	shlib_list_free();

	return (ret);
}

int
pkg_register_shlibs(struct pkg *pkg, const char *root)
{
	struct pkg_file        *file = NULL;
	char fpath[MAXPATHLEN];
	struct pkg_shlib *sh, *shtmp, *found;
	const char *origin;

	pkg_list_free(pkg, PKG_SHLIBS_REQUIRED);

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);

	shlib_list_init();
	if (shlib_list_from_elf_hints(_PATH_ELF_HINTS) != EPKG_OK) {
		shlib_list_free();
		return (EPKG_FATAL);
	}

	while(pkg_files(pkg, &file) == EPKG_OK) {
		if (root != NULL) {
			snprintf(fpath, sizeof(fpath), "%s%s", root, pkg_file_path(file));
			analyse_elf(pkg, fpath, add_shlibs_to_pkg, NULL);
		} else
			analyse_elf(pkg, pkg_file_path(file), add_shlibs_to_pkg, NULL);
	}

	pkg_get(pkg, PKG_ORIGIN, &origin);
	/*
	 * Do not depend on libraries that a package provides itself
	 */
	HASH_ITER(hh, pkg->shlibs_required, sh, shtmp) {
		HASH_FIND_STR(pkg->shlibs_provided, pkg_shlib_name(sh), found);
		if (found != NULL) {
			pkg_debug(2, "remove %s from required shlibs as the package %s provides "
					"this library itself", pkg_shlib_name(sh), origin);
			HASH_DEL(pkg->shlibs_required, sh);
		}
	}

	shlib_list_free();
	return (EPKG_OK);
}

static const char *
elf_corres_to_string(const struct _elf_corres* m, int e)
{
	int i = 0;

	for (i = 0; m[i].string != NULL; i++)
		if (m[i].elf_nb == e)
			return (m[i].string);

	return ("unknown");
}

static const char *
aeabi_parse_arm_attributes(void *data, size_t length)
{
	uint32_t sect_len;
	uint8_t *section = data;

#define	MOVE(len) do {		\
	assert(length >= (len)); \
	section += (len);	\
	length -= (len);	\
} while (0)

	if (length == 0 || *section != 'A')
		return (NULL);
	MOVE(1);

	/* Read the section length */
	if (length < sizeof(sect_len))
		return (NULL);
	memcpy(&sect_len, section, sizeof(sect_len));

	/*
	 * The section length should be no longer than the section it is within
	 */
	if (sect_len > length)
		return (NULL);

	MOVE(sizeof(sect_len));

	/* Skip the vendor name */
	while (length != 0) {
		if (*section == '\0')
			break;
		MOVE(1);
	}
	if (length == 0)
		return (NULL);
	MOVE(1);

	while (length != 0) {
		uint32_t tag_length;

		switch(*section) {
		case 1: /* Tag_File */
			MOVE(1);
			if (length < sizeof(tag_length))
				return (NULL);
			memcpy(&tag_length, section, sizeof(tag_length));
			break;
		case 2: /* Tag_Section */
		case 3: /* Tag_Symbol */
		default:
			return (NULL);
		}
		/* At least space for the tag and size */
		if (tag_length <= 5)
			return (NULL);
		tag_length--;
		/* Check the tag fits */
		if (tag_length > length)
			return (NULL);

#define	MOVE_TAG(len) do {		\
	assert(tag_length >= (len));	\
	MOVE(len);			\
	tag_length -= (len);		\
} while(0)

		MOVE(sizeof(tag_length));
		tag_length -= sizeof(tag_length);

		while (tag_length != 0) {
			uint8_t tag;

			assert(tag_length >= length);

			tag = *section;
			MOVE_TAG(1);

			/*
			 * These tag values come from:
			 * 
			 * Addenda to, and Errata in, the ABI for the
			 * ARM Architecture. Release 2.08, section 2.3.
			 */
			if (tag == 6) { /* == Tag_CPU_arch */
				uint8_t val;

				val = *section;
				/*
				 * We don't support values that require
				 * more than one byte.
				 */
				if (val & (1 << 7))
					return (NULL);

				/* We have an ARMv4 or ARMv5 */
				if (val <= 5)
					return ("arm");
				else /* We have an ARMv6+ */
					return ("armv6");
			} else if (tag == 4 || tag == 5 || tag == 32 ||
			    tag == 65 || tag == 67) {
				while (*section != '\0' && length != 0)
					MOVE_TAG(1);
				if (tag_length == 0)
					return (NULL);
				/* Skip the last byte */
				MOVE_TAG(1);
			} else if ((tag >= 7 && tag <= 31) || tag == 34 ||
			    tag == 36 || tag == 38 || tag == 42 || tag == 44 ||
			    tag == 64 || tag == 66 || tag == 68 || tag == 70) { 
				/* Skip the uleb128 data */
				while (*section & (1 << 7) && length != 0)
					MOVE_TAG(1);
				if (tag_length == 0)
					return (NULL);
				/* Skip the last byte */
				MOVE_TAG(1);
			} else
				return (NULL);
#undef MOVE_TAG
		}

		break;
	}
	return (NULL);
#undef MOVE
}

int
pkg_get_myarch(char *dest, size_t sz)
{
	Elf *elf = NULL;
	GElf_Ehdr elfhdr;
	GElf_Shdr shdr;
	Elf_Data *data;
	Elf_Note note;
	Elf_Scn *scn = NULL;
	int fd;
	char *src = NULL;
	char *osname;
	uint32_t version = 0;
	int ret = EPKG_OK;
	int i;
	const char *arch, *abi, *endian_corres_str, *wordsize_corres_str, *fpu;
	const char *path;

	path = getenv("ABI_FILE");
	if (path == NULL)
		path = _PATH_BSHELL;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pkg_emit_error("ELF library initialization failed: %s",
		    elf_errmsg(-1));
		return (EPKG_FATAL);
	}

	if ((fd = open(path, O_RDONLY)) < 0) {
		pkg_emit_errno("open", _PATH_BSHELL);
		snprintf(dest, sz, "%s", "unknown");
		return (EPKG_FATAL);
	}

	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("elf_begin() failed: %s.", elf_errmsg(-1));
		goto cleanup;
	}

	if (gelf_getehdr(elf, &elfhdr) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("getehdr() failed: %s.", elf_errmsg(-1));
		goto cleanup;
	}

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			ret = EPKG_FATAL;
			pkg_emit_error("getshdr() failed: %s.", elf_errmsg(-1));
			goto cleanup;
		}

		if (shdr.sh_type == SHT_NOTE)
			break;
	}

	if (scn == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("failed to get the note section");
		goto cleanup;
	}

	data = elf_getdata(scn, NULL);
	src = data->d_buf;
	while ((uintptr_t)src < ((uintptr_t)data->d_buf + data->d_size)) {
		memcpy(&note, src, sizeof(Elf_Note));
		src += sizeof(Elf_Note);
		if (note.n_type == NT_VERSION)
			break;
		src += note.n_namesz + note.n_descsz;
	}
	if ((uintptr_t)src >= ((uintptr_t)data->d_buf + data->d_size)) {
		ret = EPKG_FATAL;
		pkg_emit_error("failed to find the version elf note");
		goto cleanup;
	}
	osname = src;
	src += roundup2(note.n_namesz, 4);
	if (elfhdr.e_ident[EI_DATA] == ELFDATA2MSB)
		version = be32dec(src);
	else
		version = le32dec(src);

	for (i = 0; osname[i] != '\0'; i++)
		osname[i] = (char)tolower(osname[i]);

	wordsize_corres_str = elf_corres_to_string(wordsize_corres,
	    (int)elfhdr.e_ident[EI_CLASS]);

	arch = elf_corres_to_string(mach_corres, (int) elfhdr.e_machine);
#if defined(__DragonFly__)
	snprintf(dest, sz, "%s:%d.%d",
	    osname, version / 100000, (((version / 100 % 1000)+1)/2)*2);
#else
	snprintf(dest, sz, "%s:%d", osname, version / 100000);
#endif

	switch (elfhdr.e_machine) {
	case EM_ARM:
		endian_corres_str = elf_corres_to_string(endian_corres,
		    (int)elfhdr.e_ident[EI_DATA]);

		/* FreeBSD doesn't support the hard-float ABI yet */
		fpu = "softfp";
		if ((elfhdr.e_flags & 0xFF000000) != 0) {
			const char *sh_name = NULL;
			size_t shstrndx;

			/* This is an EABI file, the conformance level is set */
			abi = "eabi";

			/* Find which TARGET_ARCH we are building for. */
			elf_getshdrstrndx(elf, &shstrndx);
			while ((scn = elf_nextscn(elf, scn)) != NULL) {
				sh_name = NULL;
				if (gelf_getshdr(scn, &shdr) != &shdr) {
					scn = NULL;
					break;
				}

				sh_name = elf_strptr(elf, shstrndx,
				    shdr.sh_name);
				if (sh_name == NULL)
					continue;
				if (strcmp(".ARM.attributes", sh_name) == 0)
					break;
			}
			if (scn != NULL && sh_name != NULL) {
				data = elf_getdata(scn, NULL);
				/*
				 * Prior to FreeBSD 10.0 libelf would return
				 * NULL from elf_getdata on the .ARM.attributes
				 * section. As this was the first release to
				 * get armv6 support assume a NULL value means
				 * arm.
				 *
				 * This assumption can be removed when 9.x
				 * is unsupported.
				 */
				if (data != NULL) {
					arch = aeabi_parse_arm_attributes(
					    data->d_buf, data->d_size);
					if (arch == NULL) {
						ret = EPKG_FATAL;
						pkg_emit_error(
						    "unknown ARM ARCH");
						goto cleanup;
					}
				}
			} else {
				ret = EPKG_FATAL;
				pkg_emit_error("Unable to find the "
				    ".ARM.attributes section");
				goto cleanup;
			}

		} else if (elfhdr.e_ident[EI_OSABI] != ELFOSABI_NONE) {
			/*
			 * EABI executables all have this field set to
			 * ELFOSABI_NONE, therefore it must be an oabi file.
			 */
			abi = "oabi";
                } else {
			/*
			 * We may have failed to positively detect the ABI,
			 * set the ABI to unknown. If we end up here one of
			 * the above cases should be fixed for the binary.
			 */
			ret = EPKG_FATAL;
			pkg_emit_error("unknown ARM ABI");
			goto cleanup;
		}
		snprintf(dest + strlen(dest), sz - strlen(dest),
		    ":%s:%s:%s:%s:%s", arch, wordsize_corres_str,
		    endian_corres_str, abi, fpu);
		break;
	case EM_MIPS:
		/*
		 * this is taken from binutils sources:
		 * include/elf/mips.h
		 * mapping is figured out from binutils:
		 * gas/config/tc-mips.c
		 */
		switch (elfhdr.e_flags & EF_MIPS_ABI) {
			case E_MIPS_ABI_O32:
				abi = "o32";
				break;
			case E_MIPS_ABI_N32:
				abi = "n32";
				break;
			default:
				if (elfhdr.e_ident[EI_DATA] == ELFCLASS32)
					abi = "o32";
				else if (elfhdr.e_ident[EI_DATA] == ELFCLASS64)
					abi = "n64";
				else
					abi = "unknown";
				break;
		}
		endian_corres_str = elf_corres_to_string(endian_corres,
		    (int)elfhdr.e_ident[EI_DATA]);

		snprintf(dest + strlen(dest), sz - strlen(dest), ":%s:%s:%s:%s",
		    arch, wordsize_corres_str, endian_corres_str, abi);
		break;
	default:
		snprintf(dest + strlen(dest), sz - strlen(dest), ":%s:%s",
		    arch, wordsize_corres_str);
		break;
	}

cleanup:
	if (elf != NULL)
		elf_end(elf);

	close(fd);
	return (ret);
}

int
pkg_suggest_arch(struct pkg *pkg, const char *arch, bool isdefault)
{
	bool iswildcard;

	iswildcard = (strchr(arch, 'c') != NULL);

	if (iswildcard && isdefault)
		pkg_emit_developer_mode("Configuration error: arch \"%s\" "
		    "cannot use wildcards as default", arch);

	if (pkg->flags & (PKG_CONTAINS_ELF_OBJECTS|PKG_CONTAINS_STATIC_LIBS)) {
		if (iswildcard) {
			/* Definitely has to be arch specific */
			pkg_emit_developer_mode("Error: arch \"%s\" -- package "
			    "installs architecture specific files", arch);
		}
	} else {
		if (pkg->flags & PKG_CONTAINS_H_OR_LA) {
			if (iswildcard) {
				/* Could well be arch specific */
				pkg_emit_developer_mode("Warning: arch \"%s\" "
				    "-- package installs C/C++ headers or "
				    "libtool files,\n**** which are often "
				    "architecture specific", arch);
			}
		} else {
			/* Might be arch independent */
			if (!iswildcard)
				pkg_emit_developer_mode("Notice: arch \"%s\" -- "
				    "no architecture specific files found:\n"
				    "**** could this package use a wildcard "
				    "architecture?", arch);
		}
	}
	return (EPKG_OK);
}
