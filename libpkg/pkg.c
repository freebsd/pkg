#include <err.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>
#include <errno.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"
#include "pkg_util.h"

pkg_t
pkg_type(struct pkg const * const pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (PKG_NONE);
	}

	return (pkg->type);
}

const char *
pkg_get(struct pkg const * const pkg, const pkg_attr attr)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (NULL);
	}

	if (attr > PKG_NUM_FIELDS) {
		ERROR_BAD_ARG("attr");
		return (NULL);
	}

	return (sbuf_get(pkg->fields[attr].value));
}

int
pkg_set(struct pkg * pkg, pkg_attr attr, const char *value)
{
	struct sbuf **sbuf;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (attr > PKG_NUM_FIELDS)
		return (ERROR_BAD_ARG("attr"));

	if (value == NULL) {
		if (pkg->fields[attr].optional == 1)
			value = "";
		else
			return (ERROR_BAD_ARG("value"));
	}

	sbuf = &pkg->fields[attr].value;

	/*
	 * Ensure that mtree begins with `#mtree` so libarchive
	 * could handle it
	 */
	if (attr == PKG_MTREE && !STARTS_WITH(value, "#mtree")) {
		sbuf_set(sbuf, "#mtree\n");
		sbuf_cat(*sbuf, value);
		sbuf_finish(*sbuf);
		return (EPKG_OK);
	}

	return (sbuf_set(sbuf, value));
}

int
pkg_set_from_file(struct pkg *pkg, pkg_attr attr, const char *path)
{
	char *buf = NULL;
	off_t size = 0;
	int ret = EPKG_OK;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (path == NULL)
		return (ERROR_BAD_ARG("path"));

	if ((ret = file_to_buffer(path, &buf, &size)) !=  EPKG_OK)
		return (ret);

	ret = pkg_set(pkg, attr, buf);

	free(buf);

	return (ret);
}

int64_t
pkg_flatsize(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (-1);
	}

	return (pkg->flatsize);
}

struct pkg_script **
pkg_scripts(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (NULL);
	}

	array_init(&pkg->scripts, 1);
	return ((struct pkg_script **)pkg->scripts.data);
}

struct pkg_exec **
pkg_execs(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (NULL);
	}

	array_init(&pkg->exec, 1);
	return (struct pkg_exec **) pkg->exec.data;
}

struct pkg **
pkg_deps(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (NULL);
	}

	array_init(&pkg->deps, 1);
	return ((struct pkg **)pkg->deps.data);
}

struct pkg_option **
pkg_options(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (NULL);
	}

	array_init(&pkg->options, 1);
	return ((struct pkg_option **)pkg->options.data);
}

int
pkg_resolvdeps(struct pkg *pkg, struct pkgdb *db) {
	struct pkg *p;
	struct pkgdb_it *it;
	struct pkg **deps;
	int i;

	deps = pkg_deps(pkg);
	pkg_new(&p);
	for (i = 0; deps[i] != NULL; i++) {
		if (deps[i]->type != PKG_INSTALLED) {
			it = pkgdb_query(db, pkg_get(deps[i], PKG_ORIGIN), MATCH_EXACT);

			if (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == 0) {
				deps[i]->type = PKG_INSTALLED;
			} else {
				deps[i]->type = PKG_NOTFOUND;
			}
			pkgdb_it_free(it);
		}
	}
	pkg_free(p);

	return (EPKG_OK);
}

struct pkg **
pkg_rdeps(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (NULL);
	}

	array_init(&pkg->rdeps, 1);
	return ((struct pkg **)pkg->rdeps.data);
}

struct pkg_file **
pkg_files(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (NULL);
	}

	array_init(&pkg->files, 1);
	return ((struct pkg_file **)pkg->files.data);
}

struct pkg_conflict **
pkg_conflicts(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (NULL);
	}

	array_init(&pkg->conflicts, 1);
	return ((struct pkg_conflict **)pkg->conflicts.data);
}

int
pkg_open(struct pkg **pkg_p, const char *path)
{
	struct archive *a;
	struct archive_entry *ae;
	int ret;

	ret = pkg_open2(pkg_p, &a, &ae, path);

	if (ret != EPKG_OK && ret != EPKG_END)
		return (EPKG_FATAL);

	archive_read_finish(a);

	return (EPKG_OK);
}

int
pkg_open2(struct pkg **pkg_p, struct archive **a, struct archive_entry **ae, const char *path)
{
	struct pkg *pkg;
	struct pkg_script *script;
	pkg_error_t retcode = EPKG_OK;
	int ret;
	int64_t size;
	char *manifest;
	const char *fpath;
	char buf[2048];
	struct sbuf **sbuf;
	int i;

	struct {
		const char *name;
		pkg_attr attr;
	} files[] = {
		{ "+DESC", PKG_DESC },
		{ "+MTREE_DIRS", PKG_MTREE },
		{ NULL, 0 }
	};
	struct {
		const char *name;
		pkg_script_t type;
	} scripts[] = {
		{ "+PRE_INSTALL", PKG_SCRIPT_PRE_INSTALL },
		{ "+POST_INSTALL", PKG_SCRIPT_POST_INSTALL },
		{ "+PRE_DEINSTALL", PKG_SCRIPT_PRE_DEINSTALL },
		{ "+POST_DEINSTALL", PKG_SCRIPT_POST_DEINSTALL },
		{ "+PRE_UPGRADE", PKG_SCRIPT_PRE_UPGRADE },
		{ "+POST_UPGRADE", PKG_SCRIPT_POST_UPGRADE },
		{ "+INSTALL", PKG_SCRIPT_INSTALL },
		{ "+DEINSTALL", PKG_SCRIPT_DEINSTALL },
		{ "+UPGRADE", PKG_SCRIPT_UPGRADE },
		{ NULL, 0 }
	};

	if (path == NULL)
		return (ERROR_BAD_ARG("path"));

	*a = archive_read_new();
	archive_read_support_compression_all(*a);
	archive_read_support_format_tar(*a);

	if (archive_read_open_filename(*a, path, 4096) != ARCHIVE_OK) {
		retcode = pkg_error_set(EPKG_FATAL, "%s", archive_error_string(*a));
		goto cleanup;
	}

	if (*pkg_p == NULL)
		pkg_new(pkg_p);
	else
		pkg_reset(*pkg_p);

	pkg = *pkg_p;
	pkg->type = PKG_FILE;

	array_init(&pkg->scripts, 10);
	array_init(&pkg->files, 10);

	while ((ret = archive_read_next_header(*a, ae)) == ARCHIVE_OK) {
		fpath = archive_entry_pathname(*ae);

		if (fpath[0] != '+')
			break;

		if (strcmp(fpath, "+MANIFEST") == 0) {
			size = archive_entry_size(*ae);
			manifest = calloc(1, size + 1);
			archive_read_data(*a, manifest, size);
			ret = pkg_parse_manifest(pkg, manifest);
			free(manifest);
			if (ret != EPKG_OK) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		for (i = 0; files[i].name != NULL; i++) {
			if (strcmp(fpath, files[i].name) == 0) {
				sbuf = &pkg->fields[files[i].attr].value;
				if (*sbuf == NULL)
					*sbuf = sbuf_new_auto();
				else
					sbuf_reset(*sbuf);
				while ((size = archive_read_data(*a, buf, sizeof(buf))) > 0 ) {
					sbuf_bcat(*sbuf, buf, size);
				}
				sbuf_finish(*sbuf);
			}
		}

		for (i = 0; scripts[i].name != NULL; i++) {
			if (strcmp(fpath, scripts[i].name) == 0) {
				pkg_script_new(&script);
				script->type = scripts[i].type;
				script->data = sbuf_new_auto();
				while ((size = archive_read_data(*a, buf, sizeof(buf))) > 0 ) {
					sbuf_bcat(script->data, buf, size);
				}
				sbuf_finish(script->data);
				array_append(&pkg->scripts, script);
				break;
			}
		}
	}

	if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF)
		retcode = pkg_error_set(EPKG_FATAL, "%s", archive_error_string(*a));

	if (ret == ARCHIVE_EOF)
		retcode = EPKG_END;

	cleanup:
	if (retcode != EPKG_OK && retcode != EPKG_END) {
		if (*a != NULL)
			archive_read_finish(*a);
		*a = NULL;
		*ae = NULL;
	}

	return (retcode);
}

int
pkg_new(struct pkg **pkg)
{
	if ((*pkg = calloc(1, sizeof(struct pkg))) == NULL)
		return(pkg_error_set(EPKG_FATAL, "%s", strerror(errno)));

	(*pkg)->fields[PKG_MESSAGE].optional = 1;
	(*pkg)->fields[PKG_WWW].optional = 1;
	(*pkg)->fields[PKG_MTREE].optional = 1;

	return (EPKG_OK);
}

void
pkg_reset(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	for (int i = 0; i < PKG_NUM_FIELDS; i++)
		sbuf_reset(pkg->fields[i].value);

	pkg->flatsize = 0;
	pkg->flags = 0;
	pkg->rowid = 0;

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

	for (int i = 0; i < PKG_NUM_FIELDS; i++)
		sbuf_free(pkg->fields[i].value);

	array_free(&pkg->deps, &pkg_free_void);
	array_free(&pkg->rdeps, &pkg_free_void);
	array_free(&pkg->conflicts, &pkg_conflict_free_void);
	array_free(&pkg->files, &free);
	array_free(&pkg->scripts, &pkg_script_free_void);
	array_free(&pkg->exec, &pkg_exec_free_void);
	array_free(&pkg->options, &pkg_option_free_void);

	free(pkg);
}

void
pkg_free_void(void *p)
{
	if (p != NULL)
		pkg_free((struct pkg*) p);
}

int
pkg_setflatsize(struct pkg *pkg, int64_t size)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (size < 0)
		return (ERROR_BAD_ARG("size"));

	pkg->flatsize = size;
	return (EPKG_OK);
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
		return (ERROR_BAD_ARG("pkg"));

	if (path == NULL)
		return (ERROR_BAD_ARG("path"));

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
		return (pkg_error_set(EPKG_FATAL, "unknown script"));
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
		return (ERROR_BAD_ARG("pkg"));

	if (cmd == NULL || cmd[0] == '\0')
		return (ERROR_BAD_ARG("cmd"));

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
		return (ERROR_BAD_ARG("pkg"));

	if (opt == NULL || opt[0] == '\0')
		return (ERROR_BAD_ARG("opt"));

	if (value == NULL || value[0] == '\0')
		return (ERROR_BAD_ARG("value"));

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
		return (ERROR_BAD_ARG("pkg"));

	if (name == NULL || name[0] == '\0')
		return (ERROR_BAD_ARG("name"));

	if (origin == NULL || origin[0] == '\0')
		return (ERROR_BAD_ARG("origin"));

	if (version == NULL || version[0] == '\0')
		return (ERROR_BAD_ARG("version"));

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
pkg_addfile(struct pkg *pkg, const char *path, const char *sha256)
{
	struct pkg_file *file;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (path == NULL || path[0] == '\0')
		return (ERROR_BAD_ARG("path"));

	pkg_file_new(&file);

	strlcpy(file->path, path, sizeof(file->path));

	if (sha256 != NULL)
		strlcpy(file->sha256, sha256, sizeof(file->sha256));

	array_init(&pkg->files, 10);
	array_append(&pkg->files, file);

	return (EPKG_OK);
}

int
pkg_addconflict(struct pkg *pkg, const char *glob)
{
	struct pkg_conflict *conflict;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (glob == NULL || glob[0] == '\0')
		return (ERROR_BAD_ARG("glob"));

	pkg_conflict_new(&conflict);
	sbuf_set(&conflict->glob, glob);

	array_init(&pkg->conflicts, 5);
	array_append(&pkg->conflicts, conflict);

	return (EPKG_OK);
}
