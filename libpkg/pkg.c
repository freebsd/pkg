#include <err.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>
#include <errno.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkg_util.h"

static void pkg_free_void(void*);

pkg_t
pkg_type(struct pkg *pkg)
{
	if (pkg == NULL)
		return (PKG_NONE);

	return (pkg->type);
}

const char *
pkg_get(struct pkg *pkg, pkg_attr attr)
{
	if (pkg == NULL)
		return (NULL);

	switch (attr) {
		case PKG_NAME:
			return (sbuf_get(pkg->name));
		case PKG_VERSION:
			return (sbuf_get(pkg->version));
		case PKG_COMMENT:
			return (sbuf_get(pkg->comment));
		case PKG_ORIGIN:
			return (sbuf_get(pkg->origin));
		case PKG_DESC:
			return (sbuf_get(pkg->desc));
		case PKG_MTREE:
			return (sbuf_get(pkg->mtree));
		case PKG_MESSAGE:
			return (sbuf_get(pkg->message));
		case PKG_ARCH:
			return (sbuf_get(pkg->arch));
		case PKG_OSVERSION:
			return (sbuf_get(pkg->osversion));
		case PKG_MAINTAINER:
			return (sbuf_get(pkg->maintainer));
		case PKG_WWW:
			return (sbuf_get(pkg->www));
		case PKG_PREFIX:
			return (sbuf_get(pkg->prefix));
	}

	return (NULL);
}

int
pkg_set(struct pkg *pkg, pkg_attr attr, const char *value)
{
	if (pkg == NULL)
		return (EPKG_NULL_PKG);

	if (value == NULL) {
		return (EPKG_NULL_VALUE);
	}

	switch (attr) {
		case PKG_NAME:
			return (sbuf_set(&pkg->name, value));
		case PKG_VERSION:
			return (sbuf_set(&pkg->version, value));
		case PKG_COMMENT:
			return (sbuf_set(&pkg->comment, value));
		case PKG_ORIGIN:
			return (sbuf_set(&pkg->origin, value));
		case PKG_DESC:
			return (sbuf_set(&pkg->desc, value));
		case PKG_MTREE:
			return (sbuf_set(&pkg->mtree, value));
		case PKG_MESSAGE:
			return (sbuf_set(&pkg->message, value));
		case PKG_ARCH:
			return (sbuf_set(&pkg->arch, value));
		case PKG_OSVERSION:
			return (sbuf_set(&pkg->osversion, value));
		case PKG_MAINTAINER:
			return (sbuf_set(&pkg->maintainer, value));
		case PKG_WWW:
			return (sbuf_set(&pkg->www, value));
		case PKG_PREFIX:
			/* ensure that mtree begins by #mtree so libarchive
			 * could handle it */

			if (STARTS_WITH(value, "#mtree")) {
				return (sbuf_set(&pkg->prefix, value));
			} else {
				sbuf_set(&pkg->prefix, "#mtree\n");
				sbuf_cat(pkg->prefix, value);
				return (EPKG_OK);
			}
	}

	return (EPKG_FATAL);
}

int
pkg_set_from_file(struct pkg *pkg, pkg_attr attr, const char *path)
{
	char *buf = NULL;
	off_t size = 0;
	int ret = EPKG_OK;

	if (pkg == NULL)
		return (EPKG_NULL_PKG);

	if (path == NULL)
		return (EPKG_NULL_VALUE);

	if ((ret = file_to_buffer(path, &buf, &size)) !=  EPKG_OK)
		return (ret);

	ret = pkg_set(pkg, attr, buf);

	free(buf);

	return (ret);
}

struct pkg_script **
pkg_scripts(struct pkg *pkg)
{
	if (pkg == NULL)
		return (NULL);

	return ((struct pkg_script **)pkg->scripts.data);
}

struct pkg_exec **
pkg_execs(struct pkg *pkg)
{
	if (pkg == NULL)
		return (NULL);

	return (struct pkg_exec **) pkg->exec.data;
}

struct pkg **
pkg_deps(struct pkg *pkg)
{
	if (pkg == NULL)
		return (NULL);

	return ((struct pkg **)pkg->deps.data);
}

struct pkg_option **
pkg_options(struct pkg *pkg)
{
	if (pkg == NULL)
		return (NULL);

	return ((struct pkg_option **)pkg->options.data);
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
		it = pkgdb_query(db, pkg_get(deps[i], PKG_ORIGIN), MATCH_EXACT);

		if (pkgdb_it_next_pkg(it, &p, PKG_BASIC) == 0) {
			deps[i]->type = PKG_INSTALLED;
		} else {
			deps[i]->type = PKG_NOTFOUND;
		}
		pkgdb_it_free(it);
	}
	pkg_free(p);

	return (1);
}

struct pkg **
pkg_rdeps(struct pkg *pkg)
{
	if (pkg == NULL)
		return (NULL);

	return ((struct pkg **)pkg->rdeps.data);
}

struct pkg_file **
pkg_files(struct pkg *pkg)
{
	if (pkg == NULL)
		return (NULL);

	return ((struct pkg_file **)pkg->files.data);
}

struct pkg_conflict **
pkg_conflicts(struct pkg *pkg)
{
	if (pkg == NULL)
		return (NULL);

	return ((struct pkg_conflict **)pkg->conflicts.data);
}

int
pkg_open(const char *path, struct pkg **pkg_p, int query_flags)
{
	struct archive *a;
	struct archive_entry *ae;
	struct pkg *pkg;
	struct pkg_script *script;
	struct pkg_file *file = NULL;
	int ret;
	int64_t size;
	char *manifest;
	const char *fpath;
	char buf[1024];

	if (path == NULL)
		return (EPKG_NULL_VALUE);

	/* search for http(s) or ftp(s) */
	if (STARTS_WITH(path, "http://") || STARTS_WITH(path, "https://")
			|| STARTS_WITH(path, "ftp://")) {
		/* TODO: */
		file_fetch(path, "/tmp/bla");
		path = "/tmp/bla";
	}
	(void)query_flags;

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	if (*pkg_p == NULL)
		pkg_new(pkg_p);
	else
		pkg_reset(*pkg_p);

	pkg = *pkg_p;
	pkg->type = PKG_FILE;

	if (archive_read_open_filename(a, path, 4096) != ARCHIVE_OK)
		goto error;

	/* first path to check is the archive is corrupted bye the way retreive
	 * informations */


	array_init(&pkg->scripts, 10);
	array_init(&pkg->files, 10);

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
		fpath = archive_entry_pathname(ae);

		if (strcmp(fpath, "+MANIFEST") == 0) {
			size = archive_entry_size(ae);
			manifest = calloc(1, size + 1);
			archive_read_data(a, manifest, size);
			pkg_parse_manifest(pkg, manifest);
			free(manifest);
		}

#define COPY_FILE(fname, dest)												\
	if (strcmp(fpath, fname) == 0) {										\
		dest = sbuf_new_auto();												\
		while ((size = archive_read_data(a, buf, sizeof(buf))) > 0 ) {		\
			sbuf_bcat(dest, buf, size);										\
		}																	\
		sbuf_finish(dest);													\
	}

		COPY_FILE("+DESC", pkg->desc)
		COPY_FILE("+MTREE_DIRS", pkg->mtree)

#define COPY_SCRIPT(sname, stype)											\
	if (strcmp(fpath, sname) == 0) {										\
		pkg_script_new(&script);											\
		script->type = stype;												\
		script->data = sbuf_new_auto();										\
		while ((size = archive_read_data(a, buf, sizeof(buf))) > 0 ) {		\
			sbuf_bcat(script->data, buf, size);								\
		}																	\
		sbuf_finish(script->data);											\
		array_append(&pkg->scripts, script);								\
	}

		COPY_SCRIPT("+PRE_INSTALL", PKG_SCRIPT_PRE_INSTALL)
		COPY_SCRIPT("+POST_INSTALL", PKG_SCRIPT_POST_INSTALL)
		COPY_SCRIPT("+PRE_DEINSTALL", PKG_SCRIPT_PRE_DEINSTALL)
		COPY_SCRIPT("+POST_DEINSTALL", PKG_SCRIPT_POST_DEINSTALL)
		COPY_SCRIPT("+PRE_UPGRADE", PKG_SCRIPT_PRE_UPGRADE)
		COPY_SCRIPT("+POST_UPGRADE", PKG_SCRIPT_POST_UPGRADE)
		COPY_SCRIPT("+INSTALL", PKG_SCRIPT_INSTALL)
		COPY_SCRIPT("+DEINSTALL", PKG_SCRIPT_DEINSTALL)
		COPY_SCRIPT("+UPGRADE", PKG_SCRIPT_UPGRADE)

		if (fpath[0] == '+')
			continue;

		pkg_file_new(&file);
		strlcpy(file->path, archive_entry_pathname(ae), sizeof(file->path));
		file->size = archive_entry_size(ae);
		array_append(&pkg->files, file);
	}

	if (ret != ARCHIVE_EOF)
		goto error;

	archive_read_finish(a);
	return (EPKG_OK);

error:
	archive_read_finish(a);

	return (EPKG_ERROR_ARCHIVE);
}

#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM| \
		ARCHIVE_EXTRACT_TIME  |ARCHIVE_EXTRACT_ACL | \
		ARCHIVE_EXTRACT_FFLAGS|ARCHIVE_EXTRACT_XATTR)
int
pkg_extract(const char *path)
{
	struct archive *a;
	struct archive_entry *ae;

	int ret;

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	if (archive_read_open_filename(a, path, 4096) != ARCHIVE_OK) {
		archive_read_finish(a);
		return (-1);
	}

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
		if (archive_entry_pathname(ae)[0] == '+') {
			archive_read_data_skip(a);
		} else {
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
		}
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

	return (EPKG_OK);
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
	sbuf_reset(pkg->mtree);
	sbuf_reset(pkg->message);
	sbuf_reset(pkg->arch);
	sbuf_reset(pkg->osversion);
	sbuf_reset(pkg->maintainer);
	sbuf_reset(pkg->www);
	sbuf_reset(pkg->prefix);

	array_reset(&pkg->deps, &pkg_free_void);
	array_reset(&pkg->rdeps, &pkg_free_void);
	array_reset(&pkg->conflicts, &pkg_conflict_free_void);
	array_reset(&pkg->files, &free);
	array_reset(&pkg->scripts, &pkg_script_free_void);
	array_reset(&pkg->exec, &pkg_exec_free_void);
	array_reset(&pkg->options, &pkg_option_free_void);
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
	sbuf_free(pkg->mtree);
	sbuf_free(pkg->message);
	sbuf_free(pkg->arch);
	sbuf_free(pkg->osversion);
	sbuf_free(pkg->maintainer);
	sbuf_free(pkg->www);
	sbuf_free(pkg->prefix);

	array_free(&pkg->deps, &pkg_free_void);
	array_free(&pkg->rdeps, &pkg_free_void);
	array_free(&pkg->conflicts, &pkg_conflict_free_void);
	array_free(&pkg->files, &free);
	array_free(&pkg->scripts, &pkg_script_free_void);
	array_free(&pkg->exec, &pkg_exec_free_void);
	array_free(&pkg->options, &pkg_option_free_void);

	free(pkg);
}

static void
pkg_free_void(void *p)
{
	if (p != NULL)
		pkg_free((struct pkg*) p);
}

int
pkg_addscript(struct pkg *pkg, const char *path)
{
	struct pkg_script *script;
	char *filename;
	char *raw_script;
	int ret = EPKG_OK;
	off_t sz = 0;

	if (pkg == NULL)
		return (EPKG_NULL_PKG);

	if (path == NULL)
		return (EPKG_NULL_VALUE);

	if ((ret = file_to_buffer(path, &raw_script, &sz)) != EPKG_OK)
		return (ret);

	pkg_script_new(&script);

	sbuf_set(&script->data, raw_script);

	filename = strrchr(path, '/');
	filename[0] = '\0';
	filename++;

	if (strcmp(filename, "pkg-pre-install") == 0) {
		script->type = PKG_SCRIPT_PRE_INSTALL;
	} else if (strcmp(filename, "pkg-post-install") == 0) {
		script->type = PKG_SCRIPT_POST_INSTALL;
	} else if (strcmp(filename, "pkg-install") == 0) {
		script->type = PKG_SCRIPT_INSTALL;
	} else if (strcmp(filename, "pkg-pre-deinstall") == 0) {
		script->type = PKG_SCRIPT_PRE_DEINSTALL;
	} else if (strcmp(filename, "pkg-post-deinstall") == 0) {
		script->type = PKG_SCRIPT_POST_DEINSTALL;
	} else if (strcmp(filename, "pkg-deinstall") == 0) {
		script->type = PKG_SCRIPT_DEINSTALL;
	} else if (strcmp(filename, "pkg-pre-upgrade") == 0) {
		script->type = PKG_SCRIPT_PRE_UPGRADE;
	} else if (strcmp(filename, "pkg-post-upgrade") == 0) {
		script->type = PKG_SCRIPT_POST_UPGRADE;
	} else if (strcmp(filename, "pkg-upgrade") == 0) {
		script->type = PKG_SCRIPT_UPGRADE;
	} else {
		return (EPKG_UNKNOWN_SCRIPT);
	}

	array_init(&pkg->scripts, 6);
	array_append(&pkg->scripts, script);

	return (EPKG_OK);
}

int
pkg_addexec(struct pkg *pkg, const char *cmd, pkg_exec_t type)
{
	struct pkg_exec *exec;

	if (pkg == NULL)
		return (EPKG_NULL_PKG);

	if (cmd == NULL || cmd[0] == '\0')
		return (EPKG_NULL_VALUE);

	pkg_exec_new(&exec);

	sbuf_set(&exec->cmd, cmd);
	exec->type = type;

	array_init(&pkg->exec, 5);
	array_append(&pkg->exec, exec);

	return (EPKG_OK);
}

int
pkg_addoption(struct pkg *pkg, const char *opt, const char *value)
{
	struct pkg_option *option;

	if (pkg == NULL)
		return (EPKG_NULL_PKG);

	if (opt == NULL || opt[0] == '\0' || value == NULL || value[0] == '\0')
		return (EPKG_NULL_VALUE);

	pkg_option_new(&option);

	sbuf_set(&option->opt, opt);
	sbuf_set(&option->value, value);

	array_init(&pkg->options, 5);
	array_append(&pkg->options, option);

	return (EPKG_OK);
}

int
pkg_adddep(struct pkg *pkg, const char *name, const char *origin, const char *version)
{
	struct pkg *dep;

	if (pkg == NULL)
		return (EPKG_NULL_PKG);

	if (name == NULL || name[0] == '\0' || origin == NULL || origin[0] == '\0'
		|| version == NULL || version[0] == '\0')
		return (EPKG_NULL_VALUE);

	pkg_new(&dep);

	pkg_set(dep, PKG_NAME, name);
	pkg_set(dep, PKG_ORIGIN, origin);
	pkg_set(dep, PKG_VERSION, version);
	dep->type = PKG_NOTFOUND;

	array_init(&pkg->deps, 5);
	array_append(&pkg->deps, dep);

	return (EPKG_OK);
}

int
pkg_addfile(struct pkg *pkg, const char *path, const char *sha256, int64_t sz)
{
	struct pkg_file *file;

	if (pkg == NULL)
		return (EPKG_NULL_PKG);

	if (path == NULL || path[0] == '\0')
		return (EPKG_NULL_VALUE);

	pkg_file_new(&file);

	strlcpy(file->path, path, sizeof(file->path));

	if (sha256 != NULL)
		strlcpy(file->sha256, sha256, sizeof(file->sha256));

	file->size = sz;

	array_init(&pkg->files, 10);
	array_append(&pkg->files, file);

	return (EPKG_OK);
}

int
pkg_addconflict(struct pkg *pkg, const char *glob)
{
	struct pkg_conflict *conflict;

	if (pkg == NULL)
		return (EPKG_NULL_PKG);

	if (glob == NULL || glob[0] == '\0')
		return (EPKG_NULL_VALUE);

	pkg_conflict_new(&conflict);
	sbuf_set(&conflict->glob, glob);

	array_init(&pkg->conflicts, 5);
	array_append(&pkg->conflicts, conflict);

	return (EPKG_OK);
}
