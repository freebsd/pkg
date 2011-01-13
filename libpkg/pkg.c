#include <err.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>

#include "pkg.h"
#include "pkg_private.h"
#include "util.h"

static void pkg_free_void(void*);

pkg_t
pkg_type(struct pkg *pkg)
{
	return (pkg->type);
}

const char *
pkg_origin(struct pkg *pkg)
{
	return (sbuf_get(pkg->origin));
}

const char *
pkg_name(struct pkg *pkg)
{
	return (sbuf_get(pkg->name));
}

const char *
pkg_version(struct pkg *pkg)
{
	return (sbuf_get(pkg->version));
}

const char *
pkg_comment(struct pkg *pkg)
{
	return (sbuf_get(pkg->comment));
}

const char *
pkg_desc(struct pkg *pkg)
{
	return (sbuf_get(pkg->desc));
}

struct pkg **
pkg_deps(struct pkg *pkg)
{
	return ((struct pkg **)pkg->deps.data);
}

int
pkg_resolvdeps(struct pkg *pkg, struct pkgdb *db) {
	struct pkg *p;
	struct pkgdb_it *it;
	struct pkg **deps;
	int i;

	deps = pkg_deps(pkg);
	if (deps == NULL)
		return (-1);

	pkg_new(&p);
	for (i = 0; deps[i] != NULL; i++) {
		it = pkgdb_query(db, pkg_origin(deps[i]), MATCH_EXACT);

		if (pkgdb_it_next_pkg(it, &p, PKG_BASIC) == 0) {
			deps[i]->type = PKG_INSTALLED;
		} else {
			deps[i]->type = PKG_NOTFOUND;
		}
		pkgdb_it_free(it);
	}
	pkg_free(p);

	return (0);
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
pkg_open(const char *path, struct pkg **pkg, int query_flags)
{
	struct archive *a;
	struct archive_entry *ae;
	struct pkg_file *file = NULL;
	int ret;
	int64_t size;
	char *buf;

	/* search for http(s) or ftp(s) */
	if (STARTS_WITH(path, "http://") || STARTS_WITH(path, "https://")
			|| STARTS_WITH(path, "ftp://")) {
		file_fetch(path, "/tmp/bla");
		path = "/tmp/bla";
	}
	(void)query_flags;

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	if (archive_read_open_filename(a, path, 4096) != ARCHIVE_OK) {
		archive_read_finish(a);
		return (-1);
	}

	/* first path to check is the archive is corrupted bye the way retreive
	 * informations */

	pkg_new(pkg);
	(*pkg)->type = PKG_FILE;

	array_init(&(*pkg)->deps, 5);
	array_init(&(*pkg)->conflicts, 5);
	array_init(&(*pkg)->files, 10);

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
		if (!strcmp(archive_entry_pathname(ae),"+DESC")) {
			size = archive_entry_size(ae);
			buf = calloc(1, size+1);
			archive_read_data(a, buf, size);
			pkg_setdesc(*pkg, buf);
			free(buf);
		}

		if (!strcmp(archive_entry_pathname(ae), "+MANIFEST")) {
			size = archive_entry_size(ae);
			buf = calloc(1, size + 1);
			archive_read_data(a, buf, size);
			pkg_parse_manifest(*pkg, buf);
			free(buf);
		}

		if (archive_entry_pathname(ae)[0] == '+') {
			archive_read_data_skip(a);
			continue;
		}

		pkg_file_new(&file);
		strlcpy(file->path, archive_entry_pathname(ae), sizeof(file->path));
		array_append(&(*pkg)->files, file);

		archive_read_data_skip(a);
	}

	if (ret != ARCHIVE_EOF)
		warn("Archive corrupted");

	archive_read_finish(a);

	return (0);
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

	sbuf_reset(pkg->origin);
	sbuf_reset(pkg->name);
	sbuf_reset(pkg->version);
	sbuf_reset(pkg->comment);
	sbuf_reset(pkg->desc);

	array_reset(&pkg->deps, &pkg_free_void);
	array_reset(&pkg->rdeps, &pkg_free_void);
	array_reset(&pkg->conflicts, &pkg_conflict_free_void);
	array_reset(&pkg->files, &free);
}

void
pkg_free(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	sbuf_free(pkg->name);
	sbuf_free(pkg->version);
	sbuf_free(pkg->origin);
	sbuf_free(pkg->comment);
	sbuf_free(pkg->desc);

	array_free(&pkg->deps, &pkg_free_void);
	array_free(&pkg->rdeps, &pkg_free_void);
	array_free(&pkg->conflicts, &pkg_conflict_free_void);
	array_free(&pkg->files, &free);

	free(pkg);
}

static void
pkg_free_void(void *p)
{
	if (p != NULL)
		pkg_free((struct pkg*) p);
}

/* Setters */

int
pkg_setorigin(struct pkg *pkg, const char *origin)
{
	return (sbuf_set(&pkg->origin, origin));
}

int
pkg_setname(struct pkg *pkg, const char *name)
{
	return (sbuf_set(&pkg->name, name));
}

int
pkg_setversion(struct pkg *pkg, const char *version)
{
	return (sbuf_set(&pkg->version, version));
}

int
pkg_setcomment(struct pkg *pkg, const char *comment)
{
	return (sbuf_set(&pkg->comment, comment));
}

int
pkg_setdesc_from_file(struct pkg *pkg, const char *desc_path)
{
	char *buf = NULL;
	int ret = 0;

	if (file_to_buffer(desc_path, &buf) <= 0)
		return (-1);

	ret = pkg_setdesc(pkg, buf);

	free(buf);

	return (ret);
}

int
pkg_setdesc(struct pkg *pkg, const char *desc)
{
	return (sbuf_set(&pkg->desc, desc));
}

int
pkg_adddep(struct pkg *pkg, struct pkg *dep)
{
	if (dep == NULL)
		return (-1);

	array_init(&pkg->deps, 5);
	array_append(&pkg->deps, dep);

	return (0);
}

int
pkg_addfile(struct pkg *pkg, const char *path, const char *sha256)
{
	struct pkg_file *file;
	if (path == NULL || sha256 == NULL)
		return (-1);

	pkg_file_new(&file);

	strlcpy(file->path, path, sizeof(file->path));
	strlcpy(file->sha256, sha256, sizeof(file->sha256));

	array_init(&pkg->files, 10);
	array_append(&pkg->files, file);

	return (0);
}

int
pkg_addconflict(struct pkg *pkg, const char *glob)
{
	struct pkg_conflict *conflict;

	if (glob == NULL)
		return (-1);

	pkg_conflict_new(&conflict);
	sbuf_cpy(conflict->glob, glob);
	sbuf_finish(conflict->glob);

	array_init(&pkg->conflicts, 5);
	array_append(&pkg->conflicts, conflict);

	return (0);
}
