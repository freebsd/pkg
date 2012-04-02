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
test_depends(struct pkgdb *db, struct pkg *pkg, const char *name)
{
	struct pkg_dep *dep = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *d;
	Link_map *map;
	const char *deporigin, *depname, *depversion;
	bool found;
	void *handle;
	bool shlibs = false;
	bool autodeps = false;

	pkg_config_bool(PKG_CONFIG_AUTODEPS, &autodeps);
	pkg_config_bool(PKG_CONFIG_SHLIBS, &shlibs);

	if ((handle = dlopen(name, RTLD_LAZY)) == NULL) {
		pkg_emit_error("accessing shared library %s failed -- %s", name, dlerror());
		return (EPKG_FATAL);
	}

	dlinfo(handle, RTLD_DI_LINKMAP, &map);

	/* match /lib, /lib32, /usr/lib and /usr/lib32 */
	if (strncmp(map->l_name, "/lib", 4) == 0 || strncmp(map->l_name, "/usr/lib", 7) == 0) {
		/* ignore libs from base */
		dlclose(handle);
		return (EPKG_OK);
	}

	if (shlibs)
		pkg_addshlib(pkg, name);

	if (!autodeps) {
		dlclose(handle);
		return (EPKG_OK);
	}

	if ((it = pkgdb_query_which(db, map->l_name)) == NULL) {
		dlclose(handle);
		return (EPKG_OK);
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
	return (EPKG_OK);
}

static int
analyse_elf(struct pkgdb *db, struct pkg *pkg, const char *fpath)
{
	Elf *e;
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

	if (( e = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("elf_begin() for %s failed: %s", fpath, elf_errmsg(-1)); 
		goto cleanup;
	}

	if (elf_kind(e) != ELF_K_ELF) {
		close(fd);
		return (EPKG_END); /* Not an elf file: no results */
	}

	if (gelf_getehdr(e, &elfhdr) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("getehdr() failed: %s.", elf_errmsg(-1));
		goto cleanup;
	}

	while (( scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			ret = EPKG_FATAL;
			pkg_emit_error("getshdr() for %s failed: %s", fpath, elf_errmsg(-1));
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
		}

		if (note != NULL && dynamic != NULL)
			break;
	}

	/*
	 * note == NULL means no freebsd
	 * dynamic == NULL means not a dynamic linked elf
	 */
	if (dynamic == NULL) {
		ret = EPKG_END;
		goto cleanup; /* not a dynamically linked elf: no results */
	}

	/* some freebsd binaries don't have notes like some perl modules */
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
			pkg_emit_error("getdyn() failed for %s: %s", fpath, elf_errmsg(-1)); 
			goto cleanup;
		}

		if (dyn->d_tag != DT_NEEDED)
			continue;

		test_depends(db, pkg, elf_strptr(e, sh_link, dyn->d_un.d_val));
	}

cleanup:
	if ( e != NULL)
		elf_end(e);
	close(fd);

	return (ret);
}

int
pkg_analyse_files(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_file *file = NULL;
	int ret = EPKG_OK;
	bool shlibs = false;
	bool autodeps = false;

	pkg_config_bool(PKG_CONFIG_SHLIBS, &shlibs);
	pkg_config_bool(PKG_CONFIG_AUTODEPS, &autodeps);

	if (!autodeps && !shlibs)
		return (EPKG_OK);

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);

	while (pkg_files(pkg, &file) == EPKG_OK)
		analyse_elf(db, pkg, pkg_file_get(file, PKG_FILE_PATH));

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
