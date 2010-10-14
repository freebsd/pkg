#include "pkgdb.h"
#include "pkgdb_cache.h"

static const char *
pkg_getattr(struct pkg *pkg, const char **val, const char *attr)
{
	if (*val == NULL)
		*val = pkgdb_cache_getattr(pkg, attr);

	return (*val);
}

const char *
pkg_namever(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->namever, "namever"));
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
pkg_dep(struct pkg *pkg, struct pkg *dep)
{
	/* call backend dep query */
	return (pkgdb_cache_dep(pkg, dep));
}

void
pkg_reset(struct pkg *pkg)
{
	pkg->namever = NULL;
	pkg->name = NULL;
	pkg->version = NULL;
	pkg->origin = NULL;
	pkg->comment = NULL;
	pkg->desc = NULL;
	pkg->idx = -1;
	pkg->idep = 0;
	pkg->irdep = 0;
	pkg->pdb = NULL;
}
