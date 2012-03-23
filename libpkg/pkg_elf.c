/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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
register_shlibs(struct pkg *pkg, const char *namelist[])
{
	struct pkg_shlib *s;
	const char **name;
	bool found;

	assert(pkg != NULL);

	for (name = namelist; *name != NULL; name++) {
		s = NULL;
		found = false;
		while (pkg_shlibs(pkg, &s) == EPKG_OK) {
			if (strcmp(*name, pkg_shlib_name(s)) == 0) {
				/* already registered, but that's OK */
				found = true;
				break;
			}
		}
		if ( !found ) {
			pkg_shlib_new(&s);
			sbuf_set(&s->name, *name);
			STAILQ_INSERT_TAIL(&pkg->shlibs, s, next);
		}
	}
	return (EPKG_OK);
}

static int
add_forgotten_depends(struct pkgdb *db, struct pkg *pkg, const char *namelist[])
{
	struct pkg_dep *dep = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *d;
	Link_map *map;
	const char *deporigin, *depname, *depversion;
	const char **name;
	bool found;
	void *handle;

	for (name = namelist; *name != NULL; name++) {
		handle = dlopen(*name, RTLD_LAZY);
		if (handle == NULL) {
			pkg_emit_error("accessing shared library %s failed -- %s", name, dlerror());
			return (EPKG_FATAL);
		}

		dlinfo(handle, RTLD_DI_LINKMAP, &map);

		if ((it = pkgdb_query_which(db, map->l_name)) == NULL) {
			dlclose(handle);
			break; /* shlib not from any pkg */
		}
		d = NULL;
		if (pkgdb_it_next(it, &d, PKG_LOAD_BASIC) == EPKG_OK) {
			found = false;
			pkg_get(d, PKG_ORIGIN, &deporigin, PKG_NAME, &depname, PKG_VERSION, &depversion);

			dep = NULL;
			found = false;
			while (pkg_deps(pkg, &dep) == EPKG_OK) {
				if (strcmp(pkg_dep_get(dep, PKG_DEP_ORIGIN), deporigin) == 0) {
					found = true;
					break;
				}
			}
			if (!found) {
				pkg_emit_error("adding forgotten depends (%s): %s-%s",
					       map->l_name, depname, depversion);
				pkg_adddep(pkg, depname, deporigin, depversion);
			}
			pkg_free(d);
		}
		dlclose(handle);
		pkgdb_it_free(it);
	}
	return (EPKG_OK);
}

static int
analyse_elf(const char *fpath, const char ***namelist) 
{
	Elf *e;
	Elf_Scn *scn = NULL;
	GElf_Shdr shdr;
	Elf_Data *data;
	GElf_Dyn *dyn, dyn_mem;
	struct stat sb;

	size_t numdyn;
	size_t dynidx;
	const char *osname;
	const char **name;

	int fd;

	if ((fd = open(fpath, O_RDONLY, 0)) < 0)
		return (EPKG_FATAL);
	}
	if (fstat(fd, &sb) != 0)
		pkg_emit_errno("fstat() failed for %s", fpath);
	if (sb.st_size == 0) {
		ret = EPKG_END; /* Empty file: no results */
		goto cleanup;
	}
	if (( e = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("elf_begin() for %s failed: %s", fpath, elf_errmsg(-1)); 
		goto cleanup;
	}

	if (elf_kind(e) != ELF_K_ELF) {
		close(fd);
		return (EPKG_END); /* Not an elf file: no results */
	}

	while (( scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			ret = EPKG_FATAL;
			pkg_emit_error("getshdr() for %s failed: %s", fpath, elf_errmsg(-1));
			goto cleanup;
		}
		if (shdr.sh_type == SHT_NOTE)
			break;
	}

	if  (scn != NULL) { /* Assume FreeBSD native if no note section */
		data = elf_getdata(scn, NULL);
		osname = (const char *) data->d_buf + sizeof(Elf_Note);
		if (strncasecmp(osname, "freebsd", sizeof("freebsd")) != 0) {
			ret = EPKG_END;	/* Foreign (probably linux) ELF object */
			pkg_emit_error("ignoring %s ELF object", osname);
			goto cleanup;
		}
	}

	scn = NULL;
	while (( scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			ret = EPKG_FATAL;
			pkg_emit_error("getshdr() failed for %s: %s", fpath, elf_errmsg(-1));
			goto cleanup;
		}
		if (shdr.sh_type == SHT_DYNAMIC)
			break;
	}

	if  (scn == NULL) {
		close(fd);
		return (EPKG_END); /* not dynamically linked: no results */
	}

	data = elf_getdata(scn, NULL);
	numdyn = shdr.sh_size / shdr.sh_entsize;

	if ( (*namelist = calloc(numdyn + 1, sizeof(**namelist))) == NULL ) {
		close(fd);
		return (EPKG_FATAL);
	}
	name = *namelist;

	for (dynidx = 0; dynidx < numdyn; dynidx++) {
		if ((dyn = gelf_getdyn(data, dynidx, &dyn_mem)) == NULL) {
			ret = EPKG_FATAL;
			pkg_emit_error("getdyn() failed for %s: %s", fpath, elf_errmsg(-1)); 
			goto cleanup;
		}

		if (dyn->d_tag != DT_NEEDED)
			continue;

		*name = elf_strptr(e, shdr.sh_link, dyn->d_un.d_val);
		name++;
	}

cleanup:
	if ( e != NULL)
		elf_end(e);
	close(fd);

	return (EPKG_OK);
}

int
pkg_analyse_init(void)
{
	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);
	return (EPKG_OK);
}

int
pkg_register_shlibs_for_file(struct pkg *pkg, const char *fname)
{
	const char **namelist = NULL;
	int ret = EPKG_OK;

	switch (analyse_elf(fname, &namelist)) {
		case EPKG_OK:
			ret = register_shlibs(pkg, namelist);
			break;
		case EPKG_END:
			break; /* File not dynamically linked  */
		case EPKG_FATAL:
			ret = EPKG_FATAL;
			break;
	}
	if (namelist != NULL)
		free(namelist);

	return (ret);
}

int
pkg_register_shlibs(struct pkg *pkg)
{
	struct pkg_file *file = NULL;
	int ret = EPKG_OK;

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if ((ret = pkg_register_shlibs_for_file(pkg, pkg_file_get(file, PKG_FILE_PATH))) != EPKG_OK)
			break;
	}
	return (EPKG_OK);
}

int
pkg_analyse_one_file(struct pkgdb *db, struct pkg *pkg, const char *fname)
{
	const char **namelist = NULL;
	int ret = EPKG_OK;

	switch (analyse_elf(fname, &namelist)) {
		case EPKG_OK:
			ret = add_forgotten_depends(db, pkg, namelist);
			break;
		case EPKG_END:
			break; /* File not dynamically linked  */
		case EPKG_FATAL:
			ret = EPKG_FATAL;
			break;
	}
	if (namelist != NULL)
		free(namelist);

	return (ret);
}

int
pkg_analyse_files(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_file *file = NULL;
	int ret = EPKG_OK;

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if ((ret = pkg_analyse_one_file(db, pkg, pkg_file_get(file, PKG_FILE_PATH))) != EPKG_OK)
			break;
	}
	return (ret);
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
	char *src;
	char *osname;
	uint32_t version;
	int ret = EPKG_OK;
	int i;
	const char *abi;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pkg_emit_error("ELF library initialization failed: %s", elf_errmsg(-1));
		return (EPKG_FATAL);
	}

	if ((fd = open("/bin/sh", O_RDONLY)) < 0) {
		pkg_emit_errno("open()", "");
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
	memcpy(&note, src, sizeof(Elf_Note));
	src += sizeof(Elf_Note);
	osname = src;
	src += roundup2(note.n_namesz, 4);
	if (elfhdr.e_ident[EI_DATA] == ELFDATA2MSB)
		version = be32dec(src);
	else
		version = le32dec(src);

	for (i = 0; osname[i] != '\0'; i++)
		osname[i] = (char)tolower(osname[i]);

	snprintf(dest, sz, "%s:%d:%s:%s",
	    osname,
	    version / 100000,
	    elf_corres_to_string(mach_corres, (int) elfhdr.e_machine),
	    elf_corres_to_string(wordsize_corres, (int)elfhdr.e_ident[EI_CLASS]));

	switch (elfhdr.e_machine) {
		case EM_ARM:
			snprintf(dest + strlen(dest), sz - strlen(dest), ":%s:%s:%s",
			    elf_corres_to_string(endian_corres, (int) elfhdr.e_ident[EI_DATA]),
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
			snprintf(dest + strlen(dest), sz - strlen(dest), ":%s:%s",
			    elf_corres_to_string(endian_corres, (int) elfhdr.e_ident[EI_DATA]),
			    abi);
			break;
	}

cleanup:
	if (elf != NULL)
		elf_end(elf);

	close(fd);
	return (ret);
}
