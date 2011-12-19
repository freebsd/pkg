#include <fcntl.h>
#include <dlfcn.h>
#include <gelf.h>
#include <link.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

static int
analyse_elf(struct pkgdb *db, struct pkg *pkg, const char *fpath)
{
	struct pkg_dep *dep = NULL;
	struct pkg *p = NULL;
	struct pkgdb_it *it = NULL;
	Elf *e;
	Elf_Scn *scn = NULL;
	GElf_Shdr shdr;
	Elf_Data *data;
	GElf_Dyn *dyn, dyn_mem;

	const char *pkgorigin, *pkgname, *pkgversion;
	size_t numdyn;
	size_t dynidx;
	void *handle;
	Link_map *map;
	char *name;
	bool found=false;

	int fd;

	pkg_get(pkg, PKG_ORIGIN, &pkgorigin, PKG_NAME, &pkgname, PKG_VERSION, &pkgversion);
	if ((fd = open(fpath, O_RDONLY, 0)) < 0)
		return (EPKG_FATAL);

	if (( e = elf_begin(fd, ELF_C_READ, NULL)) == NULL)
		return (EPKG_FATAL);

	if (elf_kind(e) != ELF_K_ELF)
		return (EPKG_FATAL);

	while (( scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr)
			return (EPKG_FATAL);

		if (shdr.sh_type == SHT_DYNAMIC)
			break;
	}

	if  (scn == NULL)
		return (EPKG_OK);

	data = elf_getdata(scn, NULL);
	numdyn = shdr.sh_size / shdr.sh_entsize;

	for (dynidx = 0; dynidx < numdyn; dynidx++) {
		if ((dyn = gelf_getdyn(data, dynidx, &dyn_mem)) == NULL)
			return (EPKG_FATAL);

		if (dyn->d_tag != DT_NEEDED)
			continue;

		name = elf_strptr(e, shdr.sh_link, dyn->d_un.d_val);
		handle = dlopen(name, RTLD_LAZY);

		if (handle != NULL) {
			dlinfo(handle, RTLD_DI_LINKMAP, &map);
			if ((it = pkgdb_query_which(db, map->l_name)) == NULL)
				return (EPKG_FATAL);

			if (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
				found = false;
				while (pkg_deps(pkg, &dep) == EPKG_OK) {
					if (strcmp(pkg_dep_get(dep, PKG_DEP_ORIGIN), pkgorigin) == 0)
						found = true;
				}
				if (!found) {
					pkg_emit_error("adding forgotten depends (%s): %s-%s",
					    map->l_name, pkgname, pkgversion);
					pkg_adddep(pkg, pkgname, pkgorigin, pkgversion);
				}
			}
			dlclose(handle);
		}
		pkgdb_it_free(it);
	}
	pkg_free(p);
	close(fd);

	return (EPKG_OK);

}

int
pkg_analyse_files(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_file *file = NULL;

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);

	while (pkg_files(pkg, &file) == EPKG_OK)
		analyse_elf(db, pkg, pkg_file_get(file, PKG_FILE_PATH));

	return (EPKG_OK);
}
