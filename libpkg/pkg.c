#include <sys/param.h>

#include <assert.h>

#include "pkg.h"
#include "pkg_manifest.h"

const char *
pkg_name(struct pkg *pkg)
{
	return (pkg->name);
}

const char *
pkg_version(struct pkg *pkg)
{
	return (pkg->version);
}

const char *
pkg_comment(struct pkg *pkg)
{
	return (pkg->comment);
}

const char *
pkg_desc(struct pkg *pkg)
{
	return (pkg->comment);
}

const char *
pkg_origin(struct pkg *pkg)
{
	return (pkg->origin);
}

int
pkg_rdep(struct pkg *pkg, struct pkg *rdep)
{
	pkg_reset(rdep);
	(void)pkg;
	return (-1);
}

int
pkg_dep(struct pkg *pkg, struct pkg *dep)
{
	pkg_reset(dep);
	(void)pkg;
	return (-1);
}

void
pkg_reset(struct pkg *pkg)
{
	pkg->name = NULL;
	pkg->version = NULL;
	pkg->origin = NULL;
	pkg->comment = NULL;
	pkg->desc = NULL;
	pkg->pdb = NULL;
	pkg->m = NULL;
}

void
pkg_from_manifest(struct pkg *pkg, struct pkg_manifest *m)
{
	pkg->m = m;
}
