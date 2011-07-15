#include <archive.h>
#include <archive_entry.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_error.h"
#include "pkg_private.h"
#include "pkg_util.h"

int
pkg_new(struct pkg **pkg, pkg_t type)
{
	if ((*pkg = calloc(1, sizeof(struct pkg))) == NULL) {
		EMIT_ERRNO("malloc", "");
		return EPKG_FATAL;
	}

	struct _fields {
		int id;
		int type;
		int optional;
	} fields[] = {
		{PKG_ORIGIN, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_NAME, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_VERSION, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_COMMENT, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_DESC, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_MTREE, PKG_FILE|PKG_INSTALLED|PKG_UPGRADE, 1},
		{PKG_MESSAGE, PKG_FILE|PKG_INSTALLED|PKG_UPGRADE, 1},
		{PKG_ARCH, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_OSVERSION, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_MAINTAINER, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_WWW, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 1},
		{PKG_PREFIX, PKG_FILE|PKG_REMOTE|PKG_INSTALLED|PKG_UPGRADE, 0},
		{PKG_REPOPATH, PKG_REMOTE|PKG_UPGRADE, 0},
		{PKG_CKSUM, PKG_REMOTE|PKG_UPGRADE, 0},
		{PKG_NEWVERSION, PKG_UPGRADE, 0},
	};

	for (int i = 0; i < PKG_NUM_FIELDS; i++) {
		(*pkg)->fields[fields[i].id].type = fields[i].type;
		(*pkg)->fields[fields[i].id].optional = fields[i].optional;
	}

	STAILQ_INIT(&(*pkg)->deps);
	STAILQ_INIT(&(*pkg)->rdeps);
	STAILQ_INIT(&(*pkg)->files);
	STAILQ_INIT(&(*pkg)->dirs);
	STAILQ_INIT(&(*pkg)->conflicts);
	STAILQ_INIT(&(*pkg)->scripts);
	STAILQ_INIT(&(*pkg)->options);

	(*pkg)->automatic = false;
	(*pkg)->type = type;

	return (EPKG_OK);
}

void
pkg_reset(struct pkg *pkg, pkg_t type)
{
	if (pkg == NULL)
		return;

	for (int i = 0; i < PKG_NUM_FIELDS; i++)
		sbuf_reset(pkg->fields[i].value);

	pkg->flatsize = 0;
	pkg->new_flatsize = 0;
	pkg->new_pkgsize = 0;
	pkg->flags = 0;
	pkg->rowid = 0;

	pkg_freedeps(pkg);
	pkg_freerdeps(pkg);
	pkg_freefiles(pkg);
	pkg_freedirs(pkg);
	pkg_freeconflicts(pkg);
	pkg_freescripts(pkg);
	pkg_freeoptions(pkg);

	pkg->type = type;
}

void
pkg_free(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	for (int i = 0; i < PKG_NUM_FIELDS; i++)
		sbuf_free(pkg->fields[i].value);

	pkg_freedeps(pkg);
	pkg_freerdeps(pkg);
	pkg_freefiles(pkg);
	pkg_freedirs(pkg);
	pkg_freeconflicts(pkg);
	pkg_freescripts(pkg);
	pkg_freeoptions(pkg);

	free(pkg);
}

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

	if ((pkg->fields[attr].type & pkg->type) == 0)
		errx(EX_SOFTWARE, "wrong usage of `attr` for this type of `pkg`");

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

int
pkg_setautomatic(struct pkg *pkg)
{
	pkg->automatic = true;

	return (EPKG_OK);
}

int
pkg_isautomatic(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (-1);
	}

	return (pkg->automatic);
}

int64_t
pkg_new_flatsize(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (-1);
	}

	return (pkg->new_flatsize);
}

int64_t
pkg_new_pkgsize(struct pkg *pkg)
{
	if (pkg == NULL) {
		ERROR_BAD_ARG("pkg");
		return (-1);
	}

	return (pkg->new_pkgsize);
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
pkg_setnewflatsize(struct pkg *pkg, int64_t size)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (size <0)
		return (ERROR_BAD_ARG("size"));

	pkg->new_flatsize = size;

	return (EPKG_OK);
}

int
pkg_setnewpkgsize(struct pkg *pkg, int64_t size)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (size <0)
		return (ERROR_BAD_ARG("size"));

	pkg->new_pkgsize = size;

	return (EPKG_OK);
}

int
pkg_deps(struct pkg *pkg, struct pkg_dep **d)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (*d == NULL)
		*d = STAILQ_FIRST(&pkg->deps);
	else
		*d = STAILQ_NEXT(*d, next);

	if (*d == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_rdeps(struct pkg *pkg, struct pkg_dep **d)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (*d == NULL)
		*d = STAILQ_FIRST(&pkg->rdeps);
	else
		*d = STAILQ_NEXT(*d, next);

	if (*d == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_files(struct pkg *pkg, struct pkg_file **f)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (*f == NULL)
		*f = STAILQ_FIRST(&pkg->files);
	else
		*f = STAILQ_NEXT(*f, next);

	if (*f == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_dirs(struct pkg *pkg, struct pkg_dir **d)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (*d == NULL)
		*d = STAILQ_FIRST(&pkg->dirs);
	else
		*d = STAILQ_NEXT(*d, next);

	if (*d == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_conflicts(struct pkg *pkg, struct pkg_conflict **c)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (*c == NULL)
		*c = STAILQ_FIRST(&pkg->conflicts);
	else
		*c = STAILQ_NEXT(*c, next);

	if (*c == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_scripts(struct pkg *pkg, struct pkg_script **s)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (*s == NULL)
		*s = STAILQ_FIRST(&pkg->scripts);
	else
		*s = STAILQ_NEXT(*s, next);

	if (*s == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_options(struct pkg *pkg, struct pkg_option **o)
{
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (*o == NULL)
		*o = STAILQ_FIRST(&pkg->options);
	else
		*o = STAILQ_NEXT(*o, next);

	if (*o == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_adddep(struct pkg *pkg, const char *name, const char *origin, const char *version)
{
	struct pkg_dep *d;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (name == NULL || name[0] == '\0')
		return (ERROR_BAD_ARG("name"));

	if (origin == NULL || origin[0] == '\0')
		return (ERROR_BAD_ARG("origin"));

	if (version == NULL || version[0] == '\0')
		return (ERROR_BAD_ARG("version"));

	pkg_dep_new(&d);

	sbuf_set(&d->origin, origin);
	sbuf_set(&d->name, name);
	sbuf_set(&d->version, version);

	STAILQ_INSERT_TAIL(&pkg->deps, d, next);

	return (EPKG_OK);
}

int
pkg_addrdep(struct pkg *pkg, const char *name, const char *origin, const char *version)
{
	struct pkg_dep *d;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (name == NULL || name[0] == '\0')
		return (ERROR_BAD_ARG("name"));

	if (origin == NULL || origin[0] == '\0')
		return (ERROR_BAD_ARG("origin"));

	if (version == NULL || version[0] == '\0')
		return (ERROR_BAD_ARG("version"));

	pkg_dep_new(&d);

	sbuf_set(&d->origin, origin);
	sbuf_set(&d->name, name);
	sbuf_set(&d->version, version);

	STAILQ_INSERT_TAIL(&pkg->rdeps, d, next);

	return (EPKG_OK);
}

int
pkg_addfile(struct pkg *pkg, const char *path, const char *sha256)
{
	struct pkg_file *f;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (path == NULL || path[0] == '\0')
		return (ERROR_BAD_ARG("path"));

	pkg_file_new(&f);

	strlcpy(f->path, path, sizeof(f->path));

	if (sha256 != NULL)
		strlcpy(f->sha256, sha256, sizeof(f->sha256));

	STAILQ_INSERT_TAIL(&pkg->files, f, next);

	return (EPKG_OK);
}

int
pkg_adddir(struct pkg *pkg, const char *path)
{
	struct pkg_dir *d = NULL;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (path == NULL || path[0] == '\0')
		return (ERROR_BAD_ARG("path"));

	while (pkg_dirs(pkg, &d) == EPKG_OK) {
		if (strcmp(path, pkg_dir_path(d)) == 0) {
			warnx("Duplicate directory listing: %s, ignoring", path);
			return (EPKG_OK);
		}
	}

	pkg_dir_new(&d);
	strlcpy(d->path, path, sizeof(d->path));

	STAILQ_INSERT_TAIL(&pkg->dirs, d, next);

	return (EPKG_OK);
}

int
pkg_addconflict(struct pkg *pkg, const char *glob)
{
	struct pkg_conflict *c;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (glob == NULL || glob[0] == '\0')
		return (ERROR_BAD_ARG("glob"));

	pkg_conflict_new(&c);
	sbuf_set(&c->glob, glob);

	STAILQ_INSERT_TAIL(&pkg->conflicts, c, next);

	return (EPKG_OK);
}

int
pkg_addscript(struct pkg *pkg, const char *data, pkg_script_t type)
{
	struct pkg_script *s;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	pkg_script_new(&s);
	sbuf_set(&s->data, data);
	s->type = type;

	STAILQ_INSERT_TAIL(&pkg->scripts, s, next);

	return (EPKG_OK);
}

int
pkg_addscript_file(struct pkg *pkg, const char *path)
{
	char *filename;
	char *data;
	pkg_script_t type;
	int ret = EPKG_OK;
	off_t sz = 0;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (path == NULL)
		return (ERROR_BAD_ARG("path"));

	if ((ret = file_to_buffer(path, &data, &sz)) != EPKG_OK)
		return (ret);

	filename = strrchr(path, '/');
	filename[0] = '\0';
	filename++;

	if (strcmp(filename, "pkg-pre-install") == 0 || 
			strcmp(filename, "+PRE_INSTALL") == 0) {
		type = PKG_SCRIPT_PRE_INSTALL;
	} else if (strcmp(filename, "pkg-post-install") == 0 ||
			strcmp(filename, "+POST_INSTALL") == 0) {
		type = PKG_SCRIPT_POST_INSTALL;
	} else if (strcmp(filename, "pkg-install") == 0 ||
			strcmp(filename, "+INSTALL") == 0) {
		type = PKG_SCRIPT_INSTALL;
	} else if (strcmp(filename, "pkg-pre-deinstall") == 0 ||
			strcmp(filename, "+PRE_DEINSTALL") == 0) {
		type = PKG_SCRIPT_PRE_DEINSTALL;
	} else if (strcmp(filename, "pkg-post-deinstall") == 0 ||
			strcmp(filename, "+POST_DEINSTALL") == 0) {
		type = PKG_SCRIPT_POST_DEINSTALL;
	} else if (strcmp(filename, "pkg-deinstall") == 0 ||
			strcmp(filename, "+DEINSTALL") == 0) {
		type = PKG_SCRIPT_DEINSTALL;
	} else if (strcmp(filename, "pkg-pre-upgrade") == 0 ||
			strcmp(filename, "+PRE_UPGRADE") == 0) {
		type = PKG_SCRIPT_PRE_UPGRADE;
	} else if (strcmp(filename, "pkg-post-upgrade") == 0 ||
			strcmp(filename, "+POST_UPGRADE") == 0) {
		type = PKG_SCRIPT_POST_UPGRADE;
	} else if (strcmp(filename, "pkg-upgrade") == 0 ||
			strcmp(filename, "+UPGRADE") == 0) {
		type = PKG_SCRIPT_UPGRADE;
	} else {
		EMIT_PKG_ERROR("unknown script '%s'", filename);
		return EPKG_FATAL;
	}

	ret = pkg_addscript(pkg, data, type);
	free(data);
	return (ret);
}

int
pkg_appendscript(struct pkg *pkg, const char *cmd, pkg_script_t type)
{
	struct pkg_script *s = NULL;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (cmd == NULL || cmd[0] == '\0')
		return (ERROR_BAD_ARG("cmd"));

	while (pkg_scripts(pkg, &s) == EPKG_OK) {
		if (pkg_script_type(s) == type) {
			break;
		}
	}

	if (s != NULL) {
		sbuf_cat(s->data, cmd);
		sbuf_done(s->data);
		return (EPKG_OK);
	}

	pkg_script_new(&s);
	sbuf_set(&s->data, cmd);

	s->type = type;

	STAILQ_INSERT_TAIL(&pkg->scripts, s, next);

	return (EPKG_OK);
}

int
pkg_addoption(struct pkg *pkg, const char *key, const char *value)
{
	struct pkg_option *o;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (key == NULL || key[0] == '\0')
		return (ERROR_BAD_ARG("opt"));

	if (value == NULL || value[0] == '\0')
		return (ERROR_BAD_ARG("value"));

	pkg_option_new(&o);

	sbuf_set(&o->key, key);
	sbuf_set(&o->value, value);

	STAILQ_INSERT_TAIL(&pkg->options, o, next);

	return (EPKG_OK);
}

void
pkg_freedeps(struct pkg *pkg)
{
	struct pkg_dep *d;

	while (!STAILQ_EMPTY(&pkg->deps)) {
		d = STAILQ_FIRST(&pkg->deps);
		STAILQ_REMOVE_HEAD(&pkg->deps, next);
		pkg_dep_free(d);
	}
}

void
pkg_freerdeps(struct pkg *pkg)
{
	struct pkg_dep *d;

	while (!STAILQ_EMPTY(&pkg->rdeps)) {
		d = STAILQ_FIRST(&pkg->rdeps);
		STAILQ_REMOVE_HEAD(&pkg->rdeps, next);
		pkg_dep_free(d);
	}
}

void
pkg_freefiles(struct pkg *pkg)
{
	struct pkg_file *f;

	while (!STAILQ_EMPTY(&pkg->files)) {
		f = STAILQ_FIRST(&pkg->files);
		STAILQ_REMOVE_HEAD(&pkg->files, next);
		pkg_file_free(f);
	}
}

void
pkg_freedirs(struct pkg *pkg)
{
	struct pkg_dir *d;

	while (!STAILQ_EMPTY(&pkg->dirs)) {
		d = STAILQ_FIRST(&pkg->dirs);
		STAILQ_REMOVE_HEAD(&pkg->dirs, next);
		pkg_dir_free(d);
	}
}

void
pkg_freeconflicts(struct pkg *pkg)
{
	struct pkg_conflict *c;

	while (!STAILQ_EMPTY(&pkg->conflicts)) {
		c = STAILQ_FIRST(&pkg->conflicts);
		STAILQ_REMOVE_HEAD(&pkg->conflicts, next);
		pkg_conflict_free(c);
	}
}

void
pkg_freescripts(struct pkg *pkg)
{
	struct pkg_script *s;

	while (!STAILQ_EMPTY(&pkg->scripts)) {
		s = STAILQ_FIRST(&pkg->scripts);
		STAILQ_REMOVE_HEAD(&pkg->scripts, next);
		pkg_script_free(s);
	}
}

void
pkg_freeoptions(struct pkg *pkg)
{
	struct pkg_option *o;

	while (!STAILQ_EMPTY(&pkg->options)) {
		o = STAILQ_FIRST(&pkg->options);
		STAILQ_REMOVE_HEAD(&pkg->options, next);
		pkg_option_free(o);
	}
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
		{ "+MTREE_DIRS", PKG_MTREE },
		{ NULL, 0 }
	};

	if (path == NULL)
		return (ERROR_BAD_ARG("path"));

	*a = archive_read_new();
	archive_read_support_compression_all(*a);
	archive_read_support_format_tar(*a);

	if (archive_read_open_filename(*a, path, 4096) != ARCHIVE_OK) {
		EMIT_PKG_ERROR("archive_read_open_filename(%s): %s", path,
					   archive_error_string(*a));
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (*pkg_p == NULL)
		pkg_new(pkg_p, PKG_FILE);
	else
		pkg_reset(*pkg_p, PKG_FILE);

	pkg = *pkg_p;
	pkg->type = PKG_FILE;

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
	}

	if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) {
		EMIT_PKG_ERROR("archive_read_next_header(): %s",
					   archive_error_string(*a));
		retcode = EPKG_FATAL;
	}

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
pkg_copy_tree(struct pkg *pkg, const char *src, const char *dest)
{
	struct packing *pack;
	struct pkg_file *file = NULL;
	char spath[MAXPATHLEN];
	char dpath[MAXPATHLEN];

	if (packing_init(&pack, dest, 0) != EPKG_OK) {
		/* TODO */
		return EPKG_FATAL;
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		snprintf(spath, MAXPATHLEN, "%s%s", src, pkg_file_path(file));
		snprintf(dpath, MAXPATHLEN, "%s%s", dest, pkg_file_path(file));
		printf("%s -> %s\n", spath, dpath);
		packing_append_file(pack, spath, dpath);
	}

	return (packing_finish(pack));
}

