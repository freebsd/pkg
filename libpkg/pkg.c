#include <err.h>
#include <stdlib.h>

#include <sqlite3.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkgdb.h"
#include "util.h"

static void pkg_free_void(void*);

const char *
pkg_origin(struct pkg *pkg)
{
	return (sbuf_data(pkg->origin));
}

const char *
pkg_name(struct pkg *pkg)
{
	return (sbuf_data(pkg->name));
}

const char *
pkg_version(struct pkg *pkg)
{
	return (sbuf_data(pkg->version));
}

const char *
pkg_comment(struct pkg *pkg)
{
	return (sbuf_data(pkg->comment));
}

const char *
pkg_desc(struct pkg *pkg)
{
	return (sbuf_data(pkg->comment));
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

	(*pkg)->name = sbuf_new_auto();
	(*pkg)->version = sbuf_new_auto();
	(*pkg)->origin = sbuf_new_auto();
	(*pkg)->comment = sbuf_new_auto();
	(*pkg)->desc = sbuf_new_auto();

	return (0);
}

void
pkg_reset(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	sbuf_clear(pkg->name);
	sbuf_clear(pkg->version);
	sbuf_clear(pkg->origin);
	sbuf_clear(pkg->comment);
	sbuf_clear(pkg->desc);

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

	sbuf_delete(pkg->name);
	sbuf_delete(pkg->version);
	sbuf_delete(pkg->origin);
	sbuf_delete(pkg->comment);
	sbuf_delete(pkg->desc);

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
