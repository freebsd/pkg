#include <sys/param.h>

#include <err.h>
#include <stdlib.h>
#include <strings.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkg_manifest.h"
#include "pkgdb.h"

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
	return (pkgdb_query_rdep(pkg, rdep));
}

int
pkg_dep(struct pkg *pkg, struct pkg *dep)
{
	pkg_reset(dep);
	return (pkgdb_query_dep(pkg, dep));
}

int
pkg_new(struct pkg **pkg)
{
	if ((*pkg = calloc(1, sizeof(struct pkg))) == NULL)
		err(EXIT_FAILURE, "calloc()");
	return (0);
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
	if (pkg->deps_stmt != NULL) {
		sqlite3_finalize(pkg->deps_stmt);
		pkg->deps_stmt = NULL;
	}
	if (pkg->rdeps_stmt != NULL) {
		sqlite3_finalize(pkg->rdeps_stmt);
		pkg->rdeps_stmt = NULL;
	}
	if (pkg->which_stmt != NULL) {
		sqlite3_finalize(pkg->which_stmt);
		pkg->which_stmt = NULL;
	}
	if (pkg->m != NULL) {
		pkg_manifest_free(pkg->m);
		pkg->m = NULL;
	}
}

void
pkg_free(struct pkg *pkg)
{
	pkg_reset(pkg);
	free(pkg);
}

void
pkg_from_manifest(struct pkg *pkg, struct pkg_manifest *m)
{
	pkg->m = m;
}
