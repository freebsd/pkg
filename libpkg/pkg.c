#include <assert.h>

#include "pkgdb.h"
#include "pkgdb_cache.h"
#include "pkg_manifest.h"

static const char *
pkg_getattr(struct pkg *pkg, const char **val, const char *attr)
{
	if (*val == NULL) {
		if (pkg->m != NULL)
			*val = pkg_manifest_value(pkg->m, attr);
		else if (pkg->pdb != NULL)
			*val = pkgdb_cache_getattr(pkg, attr);
		/*else
			TODO: error */
	}

	return (*val);
}

const char *
pkg_name(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->name, "name"));
}

const char *
pkg_version(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->version, "version"));
}

const char *
pkg_comment(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->comment, "comment"));
}

const char *
pkg_desc(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->comment, "comment"));
}

const char *
pkg_origin(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->origin, "origin"));
}

int
pkg_rdep(struct pkg *pkg, struct pkg *rdep)
{
	pkg_reset(rdep);
	return (pkgdb_cache_rdep(pkg, rdep));
}

int
pkg_dep(struct pkg *pkg, struct pkg *dep)
{
	pkg_reset(dep);
	/* call backend dep query */
	if (pkg-> m != NULL) {
		if (pkg->idep == 0) {
			pkg_manifest_dep_init(pkg->m);
			pkg->idep++;
		}
		if (pkg_manifest_dep_next(pkg->m) == 0) {
			dep->name = pkg_manifest_dep_name(pkg->m);
			dep->version = pkg_manifest_dep_version(pkg->m);
			return (0);
		} else {
			return (-1);
		}
	} else if (pkg->pdb != NULL)
		return (pkgdb_cache_dep(pkg, dep));
	else
		return (-1);
 		/*TODO: error */
}

void
pkg_reset(struct pkg *pkg)
{
	pkg->name = NULL;
	pkg->version = NULL;
	pkg->origin = NULL;
	pkg->comment = NULL;
	pkg->desc = NULL;
	pkg->idx = -1;
	pkg->idep = 0;
	pkg->irdep = 0;
	pkg->pdb = NULL;
	pkg->m = NULL;
}

void
pkg_from_manifest(struct pkg *pkg, struct pkg_manifest *m)
{
	assert(pkg != NULL);
	assert(m != NULL);

	pkg_reset(pkg);
	pkg->m = m;
}
