/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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

#include <sys/endian.h>
#include <sys/types.h>
#include <sys/elf_common.h>
#include <sys/stat.h>

#include <ctype.h>
#include <assert.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <gelf.h>
#include <link.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "private/elf_tables.h"

static int
filter_system_shlibs(const char *name, char *path, size_t pathlen)
{
	void *handle;
	Link_map *map;

#ifdef STATIC_LINKAGE
	/* Can't use dlopen() in a statically linked program */
	return (EPKG_END);
#endif

	if ((handle = dlopen(name, RTLD_LAZY)) == NULL) {
		pkg_emit_error("accessing shared library %s failed -- %s",
		    name, dlerror());
		return (EPKG_FATAL);
	}

	dlinfo(handle, RTLD_DI_LINKMAP, &map);

	/* match /lib, /lib32, /usr/lib and /usr/lib32 */
	if (strncmp(map->l_name, "/lib", 4) == 0 ||
	    strncmp(map->l_name, "/usr/lib", 7) == 0) {
		/* ignore libs from base */
		dlclose(handle);
		return (EPKG_END);
	}

	if (path != NULL)
		strncpy(path, map->l_name, pathlen);

	dlclose(handle);
	return (EPKG_OK);
} 

/* Callback functions to process the shlib data */

/* ARGSUSED */
static int
do_nothing(__unused void *actdata, __unused struct pkg *pkg,
	   __unused const char *name)
{
	return (EPKG_OK);
}

/* ARGSUSED */
static int
add_shlibs_to_pkg(__unused void *actdata, struct pkg *pkg, const char *name)
{
	switch(filter_system_shlibs(name, NULL, 0)) {
	case EPKG_OK:		/* A non-system library */
		pkg_addshlib(pkg, name);
		return (EPKG_OK);
	case EPKG_END:		/* A system library */
		return (EPKG_OK);
	default:
		return (EPKG_FATAL);
	}
}

static int
test_depends(void *actdata, struct pkg *pkg, const char *name)
{
	struct pkgdb *db = actdata;
	struct pkg_dep *dep = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *d;
	const char *deporigin, *depname, *depversion;
	char pathbuf[MAXPATHLEN];
	bool found;
	bool shlibs = false;

	assert(db != NULL);

	pkg_config_bool(PKG_CONFIG_SHLIBS, &shlibs);

	switch(filter_system_shlibs(name, pathbuf, sizeof(pathbuf))) {
	case EPKG_OK:		/* A non-system library */
		break;
	case EPKG_END:		/* A system library */
		return (EPKG_OK);
	default:
		return (EPKG_FATAL);
	}

	if (shlibs)
		pkg_addshlib(pkg, name);

	if ((it = pkgdb_query_which(db, pathbuf)) == NULL)
		return (EPKG_OK);

	d = NULL;
	if (pkgdb_it_next(it, &d, PKG_LOAD_BASIC) == EPKG_OK) {
		found = false;
		pkg_get(d, PKG_ORIGIN, &deporigin, PKG_NAME, &depname,
		    PKG_VERSION, &depversion);

		dep = NULL;
		found = false;
		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			if (strcmp(pkg_dep_origin(dep), deporigin) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			pkg_emit_error("adding forgotten depends (%s): %s-%s",
					pathbuf, depname, depversion);
			pkg_adddep(pkg, depname, deporigin, depversion);
		}
		pkg_free(d);
	}

	pkgdb_it_free(it);
	return (EPKG_OK);
}

static int
analyse_elf(struct pkg *pkg, const char *fpath, 
	    int (action)(void *, struct pkg *, const char *), void *actdata)
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

	size_t numdyn;
	size_t sh_link;
	size_t dynidx;
	const char *osname;

	bool shlibs = false;
	bool autodeps = false;
	bool developer = false;

	pkg_config_bool(PKG_CONFIG_AUTODEPS, &autodeps);
	pkg_config_bool(PKG_CONFIG_SHLIBS, &shlibs);
	pkg_config_bool(PKG_CONFIG_DEVELOPER_MODE, &developer);

	int fd;

	if ((fd = open(fpath, O_RDONLY, 0)) < 0) {
		return (EPKG_FATAL);
	}
	if (fstat(fd, &sb) != 0)
		pkg_emit_errno("fstat() failed for %s", fpath);
	/* ignore empty files and non regular files */
	if (sb.st_size == 0 || !S_ISREG(sb.st_mode)) {
		ret = EPKG_END; /* Empty file: no results */
		goto cleanup;
	}

	if ((e = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("elf_begin() for %s failed: %s", fpath,
		    elf_errmsg(-1));
		goto cleanup;
	}

	if (elf_kind(e) != ELF_K_ELF) {
		close(fd);
		return (EPKG_END); /* Not an elf file: no results */
	}

	if (developer)
		pkg->flags |= PKG_CONTAINS_ELF_OBJECTS;

	if (!autodeps && !shlibs) {
	   ret = EPKG_OK;
	   goto cleanup;
	}

	if (gelf_getehdr(e, &elfhdr) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("getehdr() failed: %s.", elf_errmsg(-1));
		goto cleanup;
	}

	while ((scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			ret = EPKG_FATAL;
			pkg_emit_error("getshdr() for %s failed: %s", fpath,
			    elf_errmsg(-1));
			goto cleanup;
		}
		switch (shdr.sh_type) {
		case SHT_NOTE:
			note = scn;
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
		data = elf_getdata(note, NULL);
		osname = (const char *) data->d_buf + sizeof(Elf_Note);
		if (strncasecmp(osname, "freebsd", sizeof("freebsd")) != 0) {
			ret = EPKG_END;	/* Foreign (probably linux) ELF object */
			goto cleanup;
		}
	} else {
		if (elfhdr.e_ident[EI_OSABI] != ELFOSABI_FREEBSD) {
			ret = EPKG_END;
			goto cleanup;
		}
	}

	data = elf_getdata(dynamic, NULL);

	for (dynidx = 0; dynidx < numdyn; dynidx++) {
		if ((dyn = gelf_getdyn(data, dynidx, &dyn_mem)) == NULL) {
			ret = EPKG_FATAL;
			pkg_emit_error("getdyn() failed for %s: %s", fpath,
			    elf_errmsg(-1));
			goto cleanup;
		}

		if (dyn->d_tag != DT_NEEDED)
			continue;

		action(actdata, pkg, elf_strptr(e, sh_link, dyn->d_un.d_val));
	}

cleanup:
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
pkg_analyse_files(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_file *file = NULL;
	int ret = EPKG_OK;
	const char *fpath;
	bool shlibs = false;
	bool autodeps = false;
	bool developer = false;
	int (*action)(void *, struct pkg *, const char *);

	pkg_config_bool(PKG_CONFIG_SHLIBS, &shlibs);
	pkg_config_bool(PKG_CONFIG_AUTODEPS, &autodeps);
	pkg_config_bool(PKG_CONFIG_DEVELOPER_MODE, &developer);

	if (!autodeps && !shlibs && !developer)
		return (EPKG_OK);

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);

	if (autodeps)
		action = test_depends;
	else if (shlibs)
		action = add_shlibs_to_pkg;
	else
		action = do_nothing;

	/* Assume no architecture dependence, for contradiction */
	if (developer)
		pkg->flags &= ~(PKG_CONTAINS_ELF_OBJECTS |
				PKG_CONTAINS_STATIC_LIBS |
				PKG_CONTAINS_H_OR_LA);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		fpath = pkg_file_path(file);
		ret = analyse_elf(pkg, fpath, action, db);
		if (developer) {
			if (ret != EPKG_OK && ret != EPKG_END)
				return (ret);
			analyse_fpath(pkg, fpath);
		}
	}

	return (EPKG_OK);
}

int
pkg_register_shlibs(struct pkg *pkg)
{
	struct pkg_file *file = NULL;
	bool shlibs;

	pkg_config_bool(PKG_CONFIG_SHLIBS, &shlibs);

	if (!shlibs)
		return (EPKG_OK);

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);

	while(pkg_files(pkg, &file) == EPKG_OK)
		analyse_elf(pkg, pkg_file_path(file), add_shlibs_to_pkg, NULL);

	return (EPKG_OK);
}

static const char *
elf_corres_to_string(struct _elf_corres* m, int e)
{
	int i = 0;

	for (i = 0; m[i].string != NULL; i++)
		if (m[i].elf_nb == e)
			return (m[i].string);

	return ("unknown");
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
	const char *abi, *endian_corres_str, *wordsize_corres_str;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pkg_emit_error("ELF library initialization failed: %s",
		    elf_errmsg(-1));
		return (EPKG_FATAL);
	}

	if ((fd = open("/bin/sh", O_RDONLY)) < 0) {
		pkg_emit_errno("open", "/bin/sh");
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
		pkg_emit_error("fail to get the note section");
		goto cleanup;
	}

	data = elf_getdata(scn, NULL);
	src = data->d_buf;
	while (1) {
		memcpy(&note, src, sizeof(Elf_Note));
		src += sizeof(Elf_Note);
		if (note.n_type == NT_VERSION)
			break;
		src += note.n_namesz + note.n_descsz;
	}
	osname = src;
	src += note.n_namesz;
	if (elfhdr.e_ident[EI_DATA] == ELFDATA2MSB)
		version = be32dec(src);
	else
		version = le32dec(src);

	for (i = 0; osname[i] != '\0'; i++)
		osname[i] = (char)tolower(osname[i]);

	wordsize_corres_str = elf_corres_to_string(wordsize_corres,
	    (int)elfhdr.e_ident[EI_CLASS]);

	snprintf(dest, sz, "%s:%d:%s:%s",
	    osname, version / 100000,
	    elf_corres_to_string(mach_corres, (int) elfhdr.e_machine),
	    wordsize_corres_str);

	switch (elfhdr.e_machine) {
	case EM_ARM:
		endian_corres_str = elf_corres_to_string(endian_corres,
		    (int)elfhdr.e_ident[EI_DATA]);

		snprintf(dest + strlen(dest), sz - strlen(dest), ":%s:%s:%s",
		    endian_corres_str,
		    (elfhdr.e_flags & EF_ARM_NEW_ABI) > 0 ? "eabi" : "oabi",
		    (elfhdr.e_flags & EF_ARM_VFP_FLOAT) > 0 ? "softfp" : "vfp");
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
				break;
		}
		endian_corres_str = elf_corres_to_string(endian_corres,
		    (int)elfhdr.e_ident[EI_DATA]);

		snprintf(dest + strlen(dest), sz - strlen(dest), ":%s:%s",
		    endian_corres_str, abi);
		break;
	default:
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
		pkg_emit_error("Configuration error: arch \"%s\" cannot use "
		    "wildcards as default", arch);

	if (pkg->flags & (PKG_CONTAINS_ELF_OBJECTS|PKG_CONTAINS_STATIC_LIBS)) {
		if (iswildcard) {
			/* Definitely has to be arch specific */
			pkg_emit_error("Error: arch \"%s\" -- package installs "
			    "architecture specific files", arch);
		}
	} else {
		if (pkg->flags & PKG_CONTAINS_H_OR_LA) {
			if (iswildcard) {
				/* Could well be arch specific */
				pkg_emit_error("Warning: arch \"%s\" -- package"
				    " installs C/C++ headers or libtool "
				    "files,\n**** which are often architecture "
				    "specific", arch);
			}
		} else {
			/* Might be arch independent */
			if (!iswildcard)
				pkg_emit_error("Notice: arch \"%s\" -- no "
				    "architecture specific files found:\n"
				    "**** could this package use a wildcard "
				    "architecture?", arch);
		}
	}
	return (EPKG_OK);
}
