/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

static struct _fields {
	int type;
	int optional;
} fields[] = {
	[PKG_ORIGIN] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_NAME] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_VERSION] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_COMMENT] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_DESC] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_MTREE] = {PKG_FILE|PKG_INSTALLED, 1},
	[PKG_MESSAGE] = {PKG_FILE|PKG_INSTALLED, 1},
	[PKG_ARCH] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_MAINTAINER] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_WWW] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 1},
	[PKG_PREFIX] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_INFOS] = {PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 1},
	[PKG_REPOPATH] = {PKG_REMOTE, 0},
	[PKG_CKSUM] = {PKG_REMOTE, 0},
	[PKG_NEWVERSION] = {PKG_REMOTE, 1},
	[PKG_REPONAME] = {PKG_REMOTE, 1},
	[PKG_REPOURL] = {PKG_REMOTE, 1},
};

int
pkg_new(struct pkg **pkg, pkg_t type)
{
	if ((*pkg = calloc(1, sizeof(struct pkg))) == NULL) {
		pkg_emit_errno("malloc", "pkg");
		return EPKG_FATAL;
	}

	STAILQ_INIT(&(*pkg)->licenses);
	STAILQ_INIT(&(*pkg)->categories);
	STAILQ_INIT(&(*pkg)->deps);
	STAILQ_INIT(&(*pkg)->rdeps);
	STAILQ_INIT(&(*pkg)->files);
	STAILQ_INIT(&(*pkg)->dirs);
	STAILQ_INIT(&(*pkg)->scripts);
	STAILQ_INIT(&(*pkg)->options);
	STAILQ_INIT(&(*pkg)->users);
	STAILQ_INIT(&(*pkg)->groups);
	STAILQ_INIT(&(*pkg)->shlibs);

	(*pkg)->automatic = false;
	(*pkg)->type = type;
	(*pkg)->licenselogic = LICENSE_SINGLE;

	return (EPKG_OK);
}

void
pkg_reset(struct pkg *pkg, pkg_t type)
{
	int i;

	if (pkg == NULL)
		return;

	for (i = 0; i < PKG_NUM_FIELDS; i++)
		sbuf_reset(pkg->fields[i]);

	pkg->flatsize = 0;
	pkg->new_flatsize = 0;
	pkg->new_pkgsize = 0;
	pkg->time = 0;
	pkg->automatic = false;
	pkg->licenselogic = LICENSE_SINGLE;

	pkg_list_free(pkg, PKG_LICENSES);
	pkg_list_free(pkg, PKG_CATEGORIES);
	pkg_list_free(pkg, PKG_DEPS);
	pkg_list_free(pkg, PKG_RDEPS);
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg_list_free(pkg, PKG_SCRIPTS);
	pkg_list_free(pkg, PKG_OPTIONS);
	pkg_list_free(pkg, PKG_USERS);
	pkg_list_free(pkg, PKG_GROUPS);
	pkg_list_free(pkg, PKG_SHLIBS);

	pkg->rowid = 0;
	pkg->type = type;
}

void
pkg_free(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	for (int i = 0; i < PKG_NUM_FIELDS; i++)
		sbuf_free(pkg->fields[i]);

	pkg_list_free(pkg, PKG_LICENSES);
	pkg_list_free(pkg, PKG_CATEGORIES);
	pkg_list_free(pkg, PKG_DEPS);
	pkg_list_free(pkg, PKG_RDEPS);
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg_list_free(pkg, PKG_SCRIPTS);
	pkg_list_free(pkg, PKG_OPTIONS);
	pkg_list_free(pkg, PKG_USERS);
	pkg_list_free(pkg, PKG_GROUPS);
	pkg_list_free(pkg, PKG_SHLIBS);

	free(pkg);
}

pkg_t
pkg_type(struct pkg const * const pkg)
{
	assert(pkg != NULL);

	return (pkg->type);
}

int
pkg_is_valid(struct pkg *pkg)
{
	int i;

	if (pkg->type == 0) {
		pkg_emit_error("package type undefinied");
		return (EPKG_FATAL);
	}

	for (i = 0; i < PKG_NUM_FIELDS; i++) {
		if (fields[i].type & pkg->type && fields[i].optional == 0) {
			if (pkg->fields[i] == NULL || sbuf_get(pkg->fields[i])[0] == '\0')
				return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
pkg_vget(struct pkg const *const pkg, va_list ap)
{
	int attr;

	while ((attr = va_arg(ap, int)) > 0) {
		if (attr < PKG_NUM_FIELDS) {
			*va_arg(ap, const char **) = (pkg->fields[attr] != NULL)?
			    sbuf_get(pkg->fields[attr]) : NULL;
			continue;
		}
		switch (attr) {
			case PKG_FLATSIZE:
				*va_arg(ap, int64_t *) = pkg->flatsize;
				break;
			case PKG_NEW_FLATSIZE:
				*va_arg(ap, int64_t *) = pkg->new_flatsize;
				break;
			case PKG_NEW_PKGSIZE:
				*va_arg(ap, int64_t *) = pkg->new_pkgsize;
				break;
			case PKG_LICENSE_LOGIC:
				*va_arg(ap, lic_t *) = pkg->licenselogic;
				break;
			case PKG_AUTOMATIC:
				*va_arg(ap, bool *) = pkg->automatic;
				break;
			case PKG_TIME:
				*va_arg(ap, int64_t *) = pkg->time;
				break;
			case PKG_ROWID:
				*va_arg(ap, int64_t *) = pkg->rowid;
				break;
			default:
				va_arg(ap, void *); /* ignore */
				break;
		}
	}

	return (EPKG_OK);
}

int
pkg_get2(struct pkg const *const pkg, ...)
{
	int ret = EPKG_OK;
	va_list ap;

	assert(pkg != NULL);

	va_start(ap, pkg);
	ret = pkg_vget(pkg, ap);
	va_end(ap);

	return (ret);
}

static int
pkg_vset(struct pkg *pkg, va_list ap)
{
	struct pkg_config_kv *repokv = NULL;
	bool multirepos_enabled = false;
	int attr;

	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multirepos_enabled);

	while ((attr = va_arg(ap, int)) > 0) {
		if (attr < PKG_NUM_FIELDS) {
			struct sbuf **sbuf;
			const char *str = va_arg(ap, const char *);

			sbuf = &pkg->fields[attr];

			if (str == NULL)
				str = "";

			if (attr == PKG_MTREE && !STARTS_WITH(str, "#mtree")) {
				sbuf_set(sbuf, "#mtree\n");
				sbuf_cat(*sbuf, str);
				sbuf_finish(*sbuf);
				continue;
			}

			if (attr == PKG_REPONAME && multirepos_enabled) {
				while (pkg_config_list(PKG_CONFIG_REPOS, &repokv) == EPKG_OK) {
					if (strcmp(str, pkg_config_kv_get(repokv, PKG_CONFIG_KV_KEY)) == 0)
						pkg_set(pkg, PKG_REPOURL, pkg_config_kv_get(repokv, PKG_CONFIG_KV_VALUE));
				}
			}

			sbuf_set(sbuf, str);
			continue;
		}
		switch (attr) {
			case PKG_AUTOMATIC:
				pkg->automatic = (int)va_arg(ap, int64_t);
				break;
			case PKG_LICENSE_LOGIC:
				pkg->licenselogic = (lic_t)va_arg(ap, int64_t);
				break;
			case PKG_FLATSIZE:
				pkg->flatsize = va_arg(ap, int64_t);
				break;
			case PKG_NEW_FLATSIZE:
				pkg->new_flatsize = va_arg(ap, int64_t);
				break;
			case PKG_NEW_PKGSIZE:
				pkg->new_pkgsize = va_arg(ap, int64_t);
				break;
			case PKG_TIME:
				pkg->time = va_arg(ap, int64_t);
				break;
			case PKG_ROWID:
				pkg->rowid = va_arg(ap, int64_t);
				break;
			default:
				va_arg(ap, void *); /* ignore */
				break;
		}
	}

	return (EPKG_OK);
}

int
pkg_set2(struct pkg *pkg, ...)
{
	int ret = EPKG_OK;
	va_list ap;

	assert(pkg != NULL);

	va_start(ap, pkg);
	ret = pkg_vset(pkg, ap);
	va_end(ap);

	return (ret);
}

int
pkg_set_mtree(struct pkg *pkg, const char *mtree) {
	return (pkg_set(pkg, PKG_MTREE, mtree));
}


int
pkg_set_from_file(struct pkg *pkg, pkg_attr attr, const char *path)
{
	char *buf = NULL;
	off_t size = 0;
	int ret = EPKG_OK;

	assert(pkg != NULL);
	assert(path != NULL);

	if ((ret = file_to_buffer(path, &buf, &size)) !=  EPKG_OK)
		return (ret);

	ret = pkg_set(pkg, attr, buf);

	free(buf);

	return (ret);
}

#define PKG_LIST_NEXT(head, data) do { \
		if (data == NULL) \
			data = STAILQ_FIRST(head); \
		else \
			data = STAILQ_NEXT(data, next); \
		if (data == NULL) \
			return (EPKG_END); \
		else \
			return (EPKG_OK); \
	} while (0)

int
pkg_licenses(struct pkg *pkg, struct pkg_license **l)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->licenses, *l);
}

int
pkg_users(struct pkg *pkg, struct pkg_user **u)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->users, *u);
}

int
pkg_groups(struct pkg *pkg, struct pkg_group **g)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->groups, *g);
}

int
pkg_deps(struct pkg *pkg, struct pkg_dep **d)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->deps, *d);
}

int
pkg_rdeps(struct pkg *pkg, struct pkg_dep **d)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->rdeps, *d);
}

int
pkg_files(struct pkg *pkg, struct pkg_file **f)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->files, *f);
}

int
pkg_categories(struct pkg *pkg, struct pkg_category **c)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->categories, *c);
}

int
pkg_dirs(struct pkg *pkg, struct pkg_dir **d)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->dirs, *d);
}

int
pkg_scripts(struct pkg *pkg, struct pkg_script **s)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->scripts, *s);
}

int
pkg_options(struct pkg *pkg, struct pkg_option **o)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->options, *o);
}

int

pkg_shlibs(struct pkg *pkg, struct pkg_shlib **s)
{
	assert(pkg != NULL);

	PKG_LIST_NEXT(&pkg->shlibs, *s);
}

int
pkg_addlicense(struct pkg *pkg, const char *name)
{
	struct pkg_license *l = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	const char *pkgname;

	if (pkg->licenselogic == LICENSE_SINGLE && !STAILQ_EMPTY(&pkg->licenses)) {
		pkg_get(pkg, PKG_NAME, &pkgname);
		pkg_emit_error("%s have a single license which is already set",
		    pkgname);
		return (EPKG_FATAL);
	}

	while (pkg_licenses(pkg, &l) != EPKG_END) {
		if (!strcmp(name, pkg_license_name(l))) {
			pkg_emit_error("duplicate license listing: %s, ignoring", name);
			return (EPKG_OK);
		}
	}

	pkg_license_new(&l);

	sbuf_set(&l->name, name);

	STAILQ_INSERT_TAIL(&pkg->licenses, l, next);

	return (EPKG_OK);
}

int
pkg_adduid(struct pkg *pkg, const char *name, const char *uidstr)
{
	struct pkg_user *u = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	while (pkg_users(pkg, &u) != EPKG_END) {
		if (!strcmp(name, pkg_user_name(u))) {
			pkg_emit_error("duplicate user listing: %s, ignoring", name);
			return (EPKG_OK);
		}
	}

	pkg_user_new(&u);

	strlcpy(u->name, name, sizeof(u->name));

	if (uidstr != NULL)
		strlcpy(u->uidstr, uidstr, sizeof(u->uidstr));
	else
		u->uidstr[0] = '\0';

	STAILQ_INSERT_TAIL(&pkg->users, u, next);

	return (EPKG_OK);
}

int
pkg_adduser(struct pkg *pkg, const char *name)
{
	return (pkg_adduid(pkg, name, NULL));
}

int
pkg_addgid(struct pkg *pkg, const char *name, const char *gidstr)
{
	struct pkg_group *g = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	while (pkg_groups(pkg, &g) != EPKG_END) {
		if (!strcmp(name, pkg_group_name(g))) {
			pkg_emit_error("duplicate group listing: %s, ignoring", name);
			return (EPKG_OK);
		}
	}

	pkg_group_new(&g);

	strlcpy(g->name, name, sizeof(g->name));
	if (gidstr != NULL)
		strlcpy(g->gidstr, gidstr, sizeof(g->gidstr));
	else
		g->gidstr[0] = '\0';

	STAILQ_INSERT_TAIL(&pkg->groups, g, next);

	return (EPKG_OK);
}

int
pkg_addgroup(struct pkg *pkg, const char *name)
{
	return (pkg_addgid(pkg, name, NULL));
}

int
pkg_adddep(struct pkg *pkg, const char *name, const char *origin, const char *version)
{
	struct pkg_dep *d = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	assert(origin != NULL && origin[0] != '\0');
	assert(version != NULL && version[0] != '\0');

	while (pkg_deps(pkg, &d) != EPKG_END) {
		if (!strcmp(origin, pkg_dep_get(d, PKG_DEP_ORIGIN))) {
			pkg_emit_error("duplicate dependency listing: %s-%s, ignoring", name, version);
			return (EPKG_OK);
		}
	}

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

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	assert(origin != NULL && origin[0] != '\0');
	assert(version != NULL && version[0] != '\0');

	pkg_dep_new(&d);

	sbuf_set(&d->origin, origin);
	sbuf_set(&d->name, name);
	sbuf_set(&d->version, version);

	STAILQ_INSERT_TAIL(&pkg->rdeps, d, next);

	return (EPKG_OK);
}

int
pkg_addfile(struct pkg *pkg, const char *path, const char *sha256, bool check_duplicates)
{
	return (pkg_addfile_attr(pkg, path, sha256, NULL, NULL, 0, check_duplicates));
}

int
pkg_addfile_attr(struct pkg *pkg, const char *path, const char *sha256, const char *uname, const char *gname, mode_t perm, bool check_duplicates)
{
	struct pkg_file *f = NULL;

	assert(pkg != NULL);
	assert(path != NULL && path[0] != '\0');

	if (check_duplicates) {
		while (pkg_files(pkg, &f) != EPKG_END) {
			if (!strcmp(path, pkg_file_get(f, PKG_FILE_PATH))) {
				pkg_emit_error("duplicate file listing: %s, ignoring", pkg_file_get(f, PKG_FILE_PATH));
				return (EPKG_OK);
			}
		}
	}

	pkg_file_new(&f);
	strlcpy(f->path, path, sizeof(f->path));

	if (sha256 != NULL)
		strlcpy(f->sum, sha256, sizeof(f->sum));

	if (uname != NULL)
		strlcpy(f->uname, uname, sizeof(f->uname));

	if (gname != NULL)
		strlcpy(f->gname, gname, sizeof(f->gname));

	if (perm != 0)
		f->perm = perm;

	STAILQ_INSERT_TAIL(&pkg->files, f, next);

	return (EPKG_OK);
}

int
pkg_addcategory(struct pkg *pkg, const char *name)
{
	struct pkg_category *c = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	while (pkg_categories(pkg, &c) == EPKG_OK) {
		if (strcmp(name, pkg_category_name(c)) == 0) {
			pkg_emit_error("duplicate category listing: %s, ignoring", name);
			return (EPKG_OK);
		}
	}

	pkg_category_new(&c);

	sbuf_set(&c->name, name);

	STAILQ_INSERT_TAIL(&pkg->categories, c, next);

	return (EPKG_OK);
}

int
pkg_adddir(struct pkg *pkg, const char *path, bool try)
{
	return(pkg_adddir_attr(pkg, path, NULL, NULL, 0, try));
}

int
pkg_adddir_attr(struct pkg *pkg, const char *path, const char *uname, const char *gname, mode_t perm, bool try)
{
	struct pkg_dir *d = NULL;

	assert(pkg != NULL);
	assert(path != NULL && path[0] != '\0');

	while (pkg_dirs(pkg, &d) == EPKG_OK) {
		if (strcmp(path, pkg_dir_path(d)) == 0) {
			pkg_emit_error("duplicate directory listing: %s, ignoring", path);
			return (EPKG_OK);
		}
	}

	pkg_dir_new(&d);
	strlcpy(d->path, path, sizeof(d->path));

	if (uname != NULL)
		strlcpy(d->uname, uname, sizeof(d->uname));

	if (gname != NULL)
		strlcpy(d->gname, gname, sizeof(d->gname));

	if (perm != 0)
		d->perm = perm;

	d->try = try;

	STAILQ_INSERT_TAIL(&pkg->dirs, d, next);

	return (EPKG_OK);
}

int
pkg_addscript(struct pkg *pkg, const char *data, pkg_script_t type)
{
	struct pkg_script *s;

	assert(pkg != NULL);

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

	assert(pkg != NULL);
	assert(path != NULL);

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
		pkg_emit_error("unknown script '%s'", filename);
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

	assert(pkg != NULL);
	assert(cmd != NULL && cmd[0] != '\0');

	while (pkg_scripts(pkg, &s) == EPKG_OK) {
		if (pkg_script_type(s) == type) {
			break;
		}
	}

	if (s != NULL) {
		sbuf_cat(s->data, cmd);
		sbuf_finish(s->data);
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
	struct pkg_option *o = NULL;

	assert(pkg != NULL);
	assert(key != NULL && key[0] != '\0');
	assert(value != NULL && value[0] != '\0');

	while (pkg_options(pkg, &o) != EPKG_END) {
		if (!strcmp(key, pkg_option_opt(o))) {
			pkg_emit_error("duplicate options listing: %s, ignoring", key);
			return (EPKG_OK);
		}
	}
	pkg_option_new(&o);

	sbuf_set(&o->key, key);
	sbuf_set(&o->value, value);

	STAILQ_INSERT_TAIL(&pkg->options, o, next);

	return (EPKG_OK);
}

int
pkg_addshlib(struct pkg *pkg, const char *name)
{
	struct pkg_shlib *s = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	while (pkg_shlibs(pkg, &s) == EPKG_OK) {
		/* silently ignore duplicates in case of shlibs */
		if (strcmp(name, pkg_shlib_name(s)) == 0)
			return (EPKG_OK);
	}

	pkg_shlib_new(&s);

	sbuf_set(&s->name, name);

	STAILQ_INSERT_TAIL(&pkg->shlibs, s, next);

	return (EPKG_OK);
}

int
pkg_list_is_empty(struct pkg *pkg, pkg_list list) {
	switch (list) {
		case PKG_DEPS:
			return (STAILQ_EMPTY(&pkg->deps));
		case PKG_RDEPS:
			return (STAILQ_EMPTY(&pkg->rdeps));
		case PKG_LICENSES:
			return (STAILQ_EMPTY(&pkg->licenses));
		case PKG_OPTIONS:
			return (STAILQ_EMPTY(&pkg->options));
		case PKG_CATEGORIES:
			return (STAILQ_EMPTY(&pkg->categories));
		case PKG_FILES:
			return (STAILQ_EMPTY(&pkg->files));
		case PKG_DIRS:
			return (STAILQ_EMPTY(&pkg->dirs));
		case PKG_USERS:
			return (STAILQ_EMPTY(&pkg->users));
		case PKG_GROUPS:
			return (STAILQ_EMPTY(&pkg->groups));
		case PKG_SCRIPTS:
			return (STAILQ_EMPTY(&pkg->scripts));
		case PKG_SHLIBS:
			return (STAILQ_EMPTY(&pkg->shlibs));
	}
	
	return (0);
}

void
pkg_list_free(struct pkg *pkg, pkg_list list)  {
	struct pkg_dep *d;
	struct pkg_option *o;
	struct pkg_license *l;
	struct pkg_category *c;
	struct pkg_file *f;
	struct pkg_dir *dir;
	struct pkg_user *u;
	struct pkg_group *g;
	struct pkg_script *s;
	struct pkg_shlib *sl;

	switch (list) {
		case PKG_DEPS:
			LIST_FREE(&pkg->deps, d, pkg_dep_free);
			pkg->flags &= ~PKG_LOAD_DEPS;
			break;
		case PKG_RDEPS:
			LIST_FREE(&pkg->rdeps, d, pkg_dep_free);
			pkg->flags &= ~PKG_LOAD_RDEPS;
			break;
		case PKG_LICENSES:
			LIST_FREE(&pkg->licenses, l, pkg_license_free);
			pkg->flags &= ~PKG_LOAD_LICENSES;
			break;
		case PKG_OPTIONS:
			LIST_FREE(&pkg->options, o, pkg_option_free);
			pkg->flags &= ~PKG_LOAD_OPTIONS;
			break;
		case PKG_CATEGORIES:
			LIST_FREE(&pkg->categories, c, pkg_category_free);
			pkg->flags &= ~PKG_LOAD_CATEGORIES;
			break;
		case PKG_FILES:
			LIST_FREE(&pkg->files, f, pkg_file_free);
			pkg->flags &= ~PKG_LOAD_FILES;
			break;
		case PKG_DIRS:
			LIST_FREE(&pkg->dirs, dir, pkg_dir_free);
			pkg->flags &= ~PKG_LOAD_DIRS;
			break;
		case PKG_USERS:
			LIST_FREE(&pkg->users, u, pkg_user_free);
			pkg->flags &= ~PKG_LOAD_USERS;
			break;
		case PKG_GROUPS:
			LIST_FREE(&pkg->groups, g, pkg_group_free);
			pkg->flags &= ~PKG_LOAD_GROUPS;
			break;
		case PKG_SCRIPTS:
			LIST_FREE(&pkg->scripts, s, pkg_script_free);
			pkg->flags &= ~PKG_LOAD_SCRIPTS;
			break;
		case PKG_SHLIBS:
			LIST_FREE(&pkg->shlibs, sl, pkg_shlib_free);
			pkg->flags &= ~PKG_LOAD_SHLIBS;
			break;
	}
}

int
pkg_open(struct pkg **pkg_p, const char *path, struct sbuf *mbuf)
{
	struct archive *a;
	struct archive_entry *ae;
	int ret;

	ret = pkg_open2(pkg_p, &a, &ae, path, mbuf);

	if (ret != EPKG_OK && ret != EPKG_END)
		return (EPKG_FATAL);

	archive_read_finish(a);

	return (EPKG_OK);
}

int
pkg_open2(struct pkg **pkg_p, struct archive **a, struct archive_entry **ae, const char *path, struct sbuf *mbuf)
{
	struct pkg *pkg;
	pkg_error_t retcode = EPKG_OK;
	int ret;
	int64_t size;
	struct sbuf *manifest = mbuf;
	const char *fpath;
	char buf[BUFSIZ];
	struct sbuf **sbuf;
	int i;

	struct {
		const char *name;
		pkg_attr attr;
	} files[] = {
		{ "+MTREE_DIRS", PKG_MTREE },
		{ NULL, 0 }
	};

	assert(path != NULL && path[0] != '\0');

	sbuf_init(&manifest);

	*a = archive_read_new();
	archive_read_support_compression_all(*a);
	archive_read_support_format_tar(*a);

	if (archive_read_open_filename(*a, path, 4096) != ARCHIVE_OK) {
		pkg_emit_error("archive_read_open_filename(%s): %s", path,
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
			if (size <=0) {
				retcode = EPKG_FATAL;
				pkg_emit_error("%s is not a valid package: empty +MANIFEST found", path);
				goto cleanup;
			}

			while ((size = archive_read_data(*a, buf, sizeof(buf))) > 0) {
				sbuf_bcat(manifest, buf, size);
			}

			sbuf_finish(manifest);

			ret = pkg_parse_manifest(pkg, sbuf_get(manifest));
			if (ret != EPKG_OK) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		for (i = 0; files[i].name != NULL; i++) {
			if (strcmp(fpath, files[i].name) == 0) {
				sbuf = &pkg->fields[files[i].attr];
				sbuf_init(sbuf);
				while ((size = archive_read_data(*a, buf, sizeof(buf))) > 0 ) {
					sbuf_bcat(*sbuf, buf, size);
				}
				sbuf_finish(*sbuf);
			}
		}
	}

	if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) {
		pkg_emit_error("archive_read_next_header(): %s",
					   archive_error_string(*a));
		retcode = EPKG_FATAL;
	}

	if (ret == ARCHIVE_EOF)
		retcode = EPKG_END;

	if (sbuf_len(manifest) == 0) {
		retcode = EPKG_FATAL;
		pkg_emit_error("%s is not a valid package: no +MANIFEST found", path);
	}

	cleanup:
	if (mbuf == NULL)
		sbuf_delete(manifest);
	else
		sbuf_clear(manifest);

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
	struct pkg_dir *dir = NULL;
	char spath[MAXPATHLEN + 1];
	char dpath[MAXPATHLEN + 1];

	if (packing_init(&pack, dest, 0) != EPKG_OK) {
		/* TODO */
		return EPKG_FATAL;
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		snprintf(spath, sizeof(spath), "%s%s", src, pkg_dir_path(dir));
		snprintf(dpath, sizeof(dpath), "%s%s", dest, pkg_dir_path(dir));
		printf("%s -> %s\n", spath, dpath);
		packing_append_file(pack, spath, dpath);
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		snprintf(spath, sizeof(spath), "%s%s", src, pkg_file_get(file, PKG_FILE_PATH));
		snprintf(dpath, sizeof(dpath), "%s%s", dest, pkg_file_get(file, PKG_FILE_PATH));
		printf("%s -> %s\n", spath, dpath);
		packing_append_file(pack, spath, dpath);
	}


	return (packing_finish(pack));
}

void
pkg_test_filesum(struct pkg *pkg)
{
	struct pkg_file *f = NULL;
	const char *path;
	const char *sum;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];

	assert(pkg != NULL);

	while (pkg_files(pkg, &f) == EPKG_OK) {
		path = pkg_file_get(f, PKG_FILE_PATH);
		sum = pkg_file_get(f, PKG_FILE_SUM);
		if (*sum != '\0') {
			sha256_file(path, sha256);
			if (strcmp(sha256, sum) != 0)
				pkg_emit_file_mismatch(pkg, f, sum);
		}
	}
}

int64_t
pkg_recompute_flatsize(struct pkg *pkg)
{
	struct pkg_file *f = NULL;
	const char *path;
	struct hardlinks hl = { NULL, 0, 0 };
	int64_t flatsize = 0;
	struct stat st;
	bool regular = false;

	while (pkg_files(pkg, &f) == EPKG_OK) {
		path = pkg_file_get(f, PKG_FILE_PATH);
		if (lstat(path, &st) == 0) {
			regular = true;
			if (S_ISLNK(st.st_mode))
				regular = false;

			/* special case for hardlinks */
			if (st.st_nlink > 1)
				regular = is_hardlink(&hl, &st);

			if (regular)
				flatsize += st.st_size;
		}
	}
	return (flatsize);
}

int
pkg_is_installed(struct pkgdb *db, const char *origin)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	int ret = EPKG_FATAL;

	if ((it = pkgdb_query(db, origin, MATCH_EXACT)) == NULL)
		return (EPKG_FATAL);

	ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC);

	pkg_free(pkg);
	pkgdb_it_free(it);

	return (ret);
}
