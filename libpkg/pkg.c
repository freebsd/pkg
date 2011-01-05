#include <err.h>
#include <stdlib.h>

#include <sqlite3.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkgdb.h"
#include "util.h"

static void pkg_free_void(void*);

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

struct pkg **
pkg_deps(struct pkg *pkg)
{
	return ((struct pkg **)pkg->deps.data);
}

struct pkg **
pkg_rdeps(struct pkg *pkg)
{
	return ((struct pkg **)pkg->rdeps.data);
}

struct pkg_file **
pkg_files(struct pkg *pkg)
{
	return ((struct pkg_file **)pkg->files.data);
}

struct pkg_conflict **
pkg_conflicts(struct pkg *pkg)
{
	return ((struct pkg_conflict **)pkg->conflicts.data);
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
	if (pkg == NULL)
		return;

	pkg->name[0] = '\0';
	pkg->version[0] = '\0';
	pkg->origin[0] = '\0';
	pkg->comment[0] = '\0';

	free(pkg->desc);
	pkg->desc = NULL;

	array_reset(&pkg->deps, &pkg_free_void);
	array_reset(&pkg->rdeps, &pkg_free_void);
	array_reset(&pkg->conflicts, &free);
	array_reset(&pkg->files, &free);
}

void
pkg_free(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	free(pkg->desc);

	array_free(&pkg->deps, &pkg_free_void);
	array_free(&pkg->rdeps, &pkg_free_void);
	array_free(&pkg->conflicts, &free);
	array_free(&pkg->files, &free);

	free(pkg);
}

static void
pkg_free_void(void *p)
{
	if (p != NULL)
		pkg_free((struct pkg*) p);
}
