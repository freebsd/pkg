/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
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
	const char *human_desc;
	int type;
	int optional;
} fields[] = {
	[PKG_ORIGIN] = {"origin", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_NAME] = {"name", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_VERSION] = {"version", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_COMMENT] = {"comment", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_DESC] = {"description", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_MTREE] = {"mtree", PKG_FILE|PKG_INSTALLED, 1},
	[PKG_MESSAGE] = {"message", PKG_FILE|PKG_INSTALLED, 1},
	[PKG_ARCH] = {"architecture", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_MAINTAINER] = {"maintainer", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_WWW] = {"www", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_PREFIX] = {"prefix", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 0},
	[PKG_INFOS] = {"information", PKG_FILE|PKG_REMOTE|PKG_INSTALLED, 1},
	[PKG_REPOPATH] = {"repopath", PKG_REMOTE, 0},
	[PKG_CKSUM] = {"checksum", PKG_REMOTE, 0},
	[PKG_OLD_VERSION] = {"oldversion", PKG_REMOTE, 1},
	[PKG_REPONAME] = {"reponame", PKG_REMOTE, 1},
	[PKG_REPOURL] = {"repourl", PKG_REMOTE, 1},
	[PKG_DIGEST] = {"manifestdigest", PKG_REMOTE, 1},
	[PKG_REASON] = {"reason", PKG_REMOTE, 1}
};

int
pkg_new(struct pkg **pkg, pkg_t type)
{
	if ((*pkg = calloc(1, sizeof(struct pkg))) == NULL) {
		pkg_emit_errno("calloc", "pkg");
		return EPKG_FATAL;
	}

	(*pkg)->automatic = false;
	(*pkg)->locked = false;
	(*pkg)->direct = false;
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

	for (i = 0; i < PKG_NUM_SCRIPTS; i++)
		sbuf_reset(pkg->scripts[i]);

	pkg->flatsize = 0;
	pkg->old_flatsize = 0;
	pkg->pkgsize = 0;
	pkg->time = 0;
	pkg->flags = 0;
	pkg->automatic = false;
	pkg->locked = false;
	pkg->licenselogic = LICENSE_SINGLE;

	pkg_list_free(pkg, PKG_LICENSES);
	pkg_list_free(pkg, PKG_CATEGORIES);
	pkg_list_free(pkg, PKG_DEPS);
	pkg_list_free(pkg, PKG_RDEPS);
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg_list_free(pkg, PKG_OPTIONS);
	pkg_list_free(pkg, PKG_USERS);
	pkg_list_free(pkg, PKG_GROUPS);
	pkg_list_free(pkg, PKG_SHLIBS_REQUIRED);
	pkg_list_free(pkg, PKG_SHLIBS_PROVIDED);
	pkg_list_free(pkg, PKG_ANNOTATIONS);

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

	for (int i = 0; i < PKG_NUM_SCRIPTS; i++)
		sbuf_free(pkg->scripts[i]);

	pkg_list_free(pkg, PKG_LICENSES);
	pkg_list_free(pkg, PKG_CATEGORIES);
	pkg_list_free(pkg, PKG_DEPS);
	pkg_list_free(pkg, PKG_RDEPS);
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg_list_free(pkg, PKG_OPTIONS);
	pkg_list_free(pkg, PKG_USERS);
	pkg_list_free(pkg, PKG_GROUPS);
	pkg_list_free(pkg, PKG_SHLIBS_REQUIRED);
	pkg_list_free(pkg, PKG_SHLIBS_PROVIDED);
	pkg_list_free(pkg, PKG_ANNOTATIONS);

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
		pkg_emit_error("package type undefined");
		return (EPKG_FATAL);
	}

	/* Ensure that required fields are set. */
	for (i = 0; i < PKG_NUM_FIELDS; i++) {
		if ((fields[i].type & pkg->type) == 0 ||
		    fields[i].optional ||
		    pkg->fields[i] != NULL ||
		    sbuf_len(pkg->fields[i]) > 0)
			continue;
		pkg_emit_error("package field incomplete: %s",
		    fields[i].human_desc);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
pkg_vget(struct pkg const *const pkg, va_list ap)
{
	int attr;

	while ((attr = va_arg(ap, int)) > 0) {
		if (attr < PKG_NUM_FIELDS) {
			const char **var = va_arg(ap, const char **);
			*var = (pkg->fields[attr] != NULL) ?
			    sbuf_get(pkg->fields[attr]) : NULL;
			continue;
		}
		switch (attr) {
		case PKG_FLATSIZE:
			*va_arg(ap, int64_t *) = pkg->flatsize;
			break;
		case PKG_OLD_FLATSIZE:
			*va_arg(ap, int64_t *) = pkg->old_flatsize;
			break;
		case PKG_PKGSIZE:
			*va_arg(ap, int64_t *) = pkg->pkgsize;
			break;
		case PKG_LICENSE_LOGIC:
			*va_arg(ap, lic_t *) = pkg->licenselogic;
			break;
		case PKG_AUTOMATIC:
			*va_arg(ap, bool *) = pkg->automatic;
			break;
		case PKG_LOCKED:
			*va_arg(ap, bool *) = pkg->locked;
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

static void
pkg_set_repourl(struct pkg *pkg, const char *str)
{
	struct pkg_repo *r;

	r = pkg_repo_find_ident(str);
	if (r != NULL)
		pkg_set(pkg, PKG_REPOURL, pkg_repo_url(r));
}

static int
pkg_vset(struct pkg *pkg, va_list ap)
{
	int attr;

	while ((attr = va_arg(ap, int)) > 0) {
		if (attr < PKG_NUM_FIELDS) {
			struct sbuf **sbuf;
			const char *str = va_arg(ap, const char *);

			if (str == NULL) {
				pkg->fields[attr] = NULL;
				continue;
			}

			sbuf = &pkg->fields[attr];

			if (attr == PKG_MTREE && !STARTS_WITH(str, "#mtree")) {
				sbuf_set(sbuf, "#mtree\n");
				sbuf_cat(*sbuf, str);
				sbuf_finish(*sbuf);
				continue;
			}

			if (attr == PKG_REPOURL)
				pkg_set_repourl(pkg, str);

			sbuf_set(sbuf, str);
			continue;
		}
		switch (attr) {
		case PKG_AUTOMATIC:
			pkg->automatic = (int)va_arg(ap, int64_t);
			break;
		case PKG_LOCKED:
			pkg->locked = (bool)va_arg(ap, int64_t);
			break;
		case PKG_LICENSE_LOGIC:
			pkg->licenselogic = (lic_t)va_arg(ap, int64_t);
			break;
		case PKG_FLATSIZE:
			pkg->flatsize = va_arg(ap, int64_t);
			break;
		case PKG_OLD_FLATSIZE:
			pkg->old_flatsize = va_arg(ap, int64_t);
			break;
		case PKG_PKGSIZE:
			pkg->pkgsize = va_arg(ap, int64_t);
			break;
		case PKG_TIME:
			pkg->time = va_arg(ap, int64_t);
			break;
		case PKG_ROWID:
			pkg->rowid = va_arg(ap, int64_t);
			break;
		default:
			/* XXX emit an error? */
			(void) va_arg(ap, void *);
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
pkg_set_from_file(struct pkg *pkg, pkg_attr attr, const char *path, bool trimcr)
{
	char *buf = NULL;
	off_t size = 0;
	int ret = EPKG_OK;

	assert(pkg != NULL);
	assert(path != NULL);

	if ((ret = file_to_buffer(path, &buf, &size)) !=  EPKG_OK)
		return (ret);

	while (trimcr && buf[strlen(buf) - 1] == '\n')
		buf[strlen(buf) - 1] = '\0';

	ret = pkg_set(pkg, attr, buf);

	free(buf);

	return (ret);
}

int
pkg_licenses(const struct pkg *pkg, struct pkg_license **l)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->licenses, (*l));
}

int
pkg_users(const struct pkg *pkg, struct pkg_user **u)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->users, (*u));
}

int
pkg_groups(const struct pkg *pkg, struct pkg_group **g)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->groups, (*g));
}

int
pkg_deps(const struct pkg *pkg, struct pkg_dep **d)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->deps, (*d));
}

int
pkg_rdeps(const struct pkg *pkg, struct pkg_dep **d)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->rdeps, (*d));
}

int
pkg_files(const struct pkg *pkg, struct pkg_file **f)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->files, (*f));
}

int
pkg_categories(const struct pkg *pkg, struct pkg_category **c)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->categories, (*c));
}

int
pkg_dirs(const struct pkg *pkg, struct pkg_dir **d)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->dirs, (*d));
}

int
pkg_options(const struct pkg *pkg, struct pkg_option **o)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->options, (*o));
}

int
pkg_shlibs_required(const struct pkg *pkg, struct pkg_shlib **s)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->shlibs_required, (*s));
}

int
pkg_shlibs_provided(const struct pkg *pkg, struct pkg_shlib **s)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->shlibs_provided, (*s));
}

int
pkg_annotations(const struct pkg *pkg, struct pkg_note **an)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->annotations, (*an));
}

int
pkg_addlicense(struct pkg *pkg, const char *name)
{
	struct pkg_license *l = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	const char *pkgname;

	if (pkg->licenselogic == LICENSE_SINGLE && HASH_COUNT(pkg->licenses) != 0) {
		pkg_get(pkg, PKG_NAME, &pkgname);
		pkg_emit_error("%s have a single license which is already set",
		    pkgname);
		return (EPKG_FATAL);
	}

	HASH_FIND_STR(pkg->licenses, __DECONST(char *, name), l);
	if (l != NULL) {
		pkg_emit_error("duplicate license listing: %s, ignoring", name);
		return (EPKG_OK);
	}

	pkg_license_new(&l);

	strlcpy(l->name, name, sizeof(l->name));

	HASH_ADD_STR(pkg->licenses, name, l);

	return (EPKG_OK);
}

int
pkg_adduid(struct pkg *pkg, const char *name, const char *uidstr)
{
	struct pkg_user *u = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->users, __DECONST(char *, name), u);
	if (u != NULL) {
		pkg_emit_error("duplicate user listing: %s, ignoring", name);
		return (EPKG_OK);
	}

	pkg_user_new(&u);

	strlcpy(u->name, name, sizeof(u->name));

	if (uidstr != NULL)
		strlcpy(u->uidstr, uidstr, sizeof(u->uidstr));
	else
		u->uidstr[0] = '\0';

	HASH_ADD_STR(pkg->users, name, u);

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

	HASH_FIND_STR(pkg->groups, __DECONST(char *, name), g);
	if (g != NULL) {
		pkg_emit_error("duplicate group listing: %s, ignoring", name);
		return (EPKG_OK);
	}

	pkg_group_new(&g);

	strlcpy(g->name, name, sizeof(g->name));
	if (gidstr != NULL)
		strlcpy(g->gidstr, gidstr, sizeof(g->gidstr));
	else
		g->gidstr[0] = '\0';

	HASH_ADD_STR(pkg->groups, name, g);

	return (EPKG_OK);
}

int
pkg_addgroup(struct pkg *pkg, const char *name)
{
	return (pkg_addgid(pkg, name, NULL));
}

int
pkg_adddep(struct pkg *pkg, const char *name, const char *origin, const char *version, bool locked)
{
	struct pkg_dep *d = NULL;
	const char *n1, *v1;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	assert(origin != NULL && origin[0] != '\0');
	assert(version != NULL && version[0] != '\0');

	HASH_FIND_STR(pkg->deps, __DECONST(char *, origin), d);
	if (d != NULL) {
		pkg_get(pkg, PKG_NAME, &n1, PKG_VERSION, &v1);
		pkg_emit_error("%s-%s: duplicate dependency listing: %s-%s, ignoring",
		    n1, v1, name, version);
		return (EPKG_OK);
	}

	pkg_dep_new(&d);

	sbuf_set(&d->origin, origin);
	sbuf_set(&d->name, name);
	sbuf_set(&d->version, version);
	d->locked = locked;

	HASH_ADD_KEYPTR(hh, pkg->deps, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)),
	    strlen(pkg_dep_get(d, PKG_DEP_ORIGIN)), d);

	return (EPKG_OK);
}

int
pkg_addrdep(struct pkg *pkg, const char *name, const char *origin, const char *version, bool locked)
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
	d->locked = locked;

	HASH_ADD_KEYPTR(hh, pkg->rdeps, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)),
	    strlen(pkg_dep_get(d, PKG_DEP_ORIGIN)), d);

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
		HASH_FIND_STR(pkg->files, __DECONST(char *, path), f);
		if (f != NULL) {
			pkg_emit_error("duplicate file listing: %s, ignoring", pkg_file_path(f));
			return (EPKG_OK);
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

	HASH_ADD_STR(pkg->files, path, f);

	return (EPKG_OK);
}

int
pkg_addcategory(struct pkg *pkg, const char *name)
{
	struct pkg_category *c = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->categories, __DECONST(char *, name), c);
	if (c != NULL) {
		pkg_emit_error("duplicate category listing: %s, ignoring", name);
		return (EPKG_OK);
	}

	pkg_category_new(&c);

	sbuf_set(&c->name, name);

	HASH_ADD_KEYPTR(hh, pkg->categories, __DECONST(char *, pkg_category_name(c)),
	    strlen(pkg_category_name(c)), c);

	return (EPKG_OK);
}

int
pkg_adddir(struct pkg *pkg, const char *path, bool try, bool check_duplicates)
{
	return(pkg_adddir_attr(pkg, path, NULL, NULL, 0, try, check_duplicates));
}

int
pkg_adddir_attr(struct pkg *pkg, const char *path, const char *uname, const char *gname, mode_t perm, bool try, bool check_duplicates)
{
	struct pkg_dir *d = NULL;

	assert(pkg != NULL);
	assert(path != NULL && path[0] != '\0');

	if (check_duplicates) {
		HASH_FIND_STR(pkg->dirs, __DECONST(char *, path), d);
		if (d != NULL) {
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

	HASH_ADD_STR(pkg->dirs, path, d);

	return (EPKG_OK);
}

int
pkg_addscript(struct pkg *pkg, const char *data, pkg_script type)
{
	struct sbuf **sbuf;

	assert(pkg != NULL);
	sbuf = &pkg->scripts[type];
	sbuf_set(sbuf, data);

	return (EPKG_OK);
}

int
pkg_addscript_file(struct pkg *pkg, const char *path)
{
	char *filename;
	char *data;
	pkg_script type;
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
pkg_appendscript(struct pkg *pkg, const char *cmd, pkg_script type)
{
	struct sbuf **s;

	assert(pkg != NULL);
	assert(cmd != NULL && cmd[0] != '\0');

	if (pkg_script_get(pkg, type) == NULL)
		return (pkg_addscript(pkg, cmd, type));

	s = &pkg->scripts[type];
	sbuf_cat(*s, cmd);
	sbuf_finish(*s);

	return (EPKG_OK);
}

int
pkg_addoption(struct pkg *pkg, const char *key, const char *value)
{
	struct pkg_option *o = NULL;

	assert(pkg != NULL);
	assert(key != NULL && key[0] != '\0');
	assert(value != NULL && value[0] != '\0');

	HASH_FIND_STR(pkg->options, __DECONST(char *, key), o);
	if (o != NULL) {
		pkg_emit_error("duplicate options listing: %s, ignoring", key);
		return (EPKG_OK);
	}
	pkg_option_new(&o);

	sbuf_set(&o->key, key);
	sbuf_set(&o->value, value);

	HASH_ADD_KEYPTR(hh, pkg->options, __DECONST(char *, pkg_option_opt(o)),
	    strlen(pkg_option_opt(o)), o);

	return (EPKG_OK);
}

int
pkg_addshlib_required(struct pkg *pkg, const char *name)
{
	struct pkg_shlib *s = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->shlibs_required, __DECONST(char *, name), s);
	/* silently ignore duplicates in case of shlibs */
	if (s != NULL)
		return (EPKG_OK);

	pkg_shlib_new(&s);

	sbuf_set(&s->name, name);

	HASH_ADD_KEYPTR(hh, pkg->shlibs_required,
	    __DECONST(char *, pkg_shlib_name(s)),
	    strlen(pkg_shlib_name(s)), s);

	return (EPKG_OK);
}

int
pkg_addshlib_provided(struct pkg *pkg, const char *name)
{
	struct pkg_shlib *s = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->shlibs_provided, __DECONST(char *, name), s);
	/* silently ignore duplicates in case of shlibs */
	if (s != NULL)
		return (EPKG_OK);

	pkg_shlib_new(&s);

	sbuf_set(&s->name, name);

	HASH_ADD_KEYPTR(hh, pkg->shlibs_provided,
	    __DECONST(char *, pkg_shlib_name(s)),
	    strlen(pkg_shlib_name(s)), s);

	return (EPKG_OK);
}

int
pkg_addannotation(struct pkg *pkg, const char *tag, const char *value)
{
	struct pkg_note *an = NULL;

	assert(pkg != NULL);
	assert(tag != NULL);
	assert(value != NULL);

	/* Tags are unique per-package */

	HASH_FIND_STR(pkg->annotations, __DECONST(char *, tag), an);
	if (an != NULL) {
		pkg_emit_error("duplicate annotation tag: %s value: %s,"
			       " ignoring", tag, value);
		return (EPKG_OK);
	}
	an = NULL;
	pkg_annotation_new(&an);

	sbuf_set(&an->tag, tag);
	sbuf_set(&an->value, value);

	HASH_ADD_KEYPTR(hh, pkg->annotations,
	    __DECONST(char *, pkg_annotation_tag(an)),
	    strlen(pkg_annotation_tag(an)), an);

	return (EPKG_OK);
}

struct pkg_note *
pkg_annotation_lookup(const struct pkg *pkg, const char *tag)
{
	struct pkg_note *an = NULL;

	assert(pkg != NULL);
	assert(tag != NULL);

	HASH_FIND_STR(pkg->annotations, __DECONST(char *, tag), an);

	return (an);
}

int
pkg_delannotation(struct pkg *pkg, const char *tag)
{
	struct pkg_note *an = NULL;

	assert(pkg != NULL);
	assert(tag != NULL);

	HASH_FIND_STR(pkg->annotations, __DECONST(char *, tag), an);
	if (an != NULL) {
		HASH_DEL(pkg->annotations, an);
		pkg_annotation_free(an);
		return (EPKG_OK);
	} else {
		pkg_emit_error("deleting annotation tagged \'%s\' -- "
			       "not found", tag);
		return (EPKG_WARN);
	}
}

int
pkg_list_count(const struct pkg *pkg, pkg_list list)
{
	switch (list) {
	case PKG_DEPS:
		return (HASH_COUNT(pkg->deps));
	case PKG_RDEPS:
		return (HASH_COUNT(pkg->rdeps));
	case PKG_LICENSES:
		return (HASH_COUNT(pkg->licenses));
	case PKG_OPTIONS:
		return (HASH_COUNT(pkg->options));
	case PKG_CATEGORIES:
		return (HASH_COUNT(pkg->categories));
	case PKG_FILES:
		return (HASH_COUNT(pkg->files));
	case PKG_DIRS:
		return (HASH_COUNT(pkg->dirs));
	case PKG_USERS:
		return (HASH_COUNT(pkg->users));
	case PKG_GROUPS:
		return (HASH_COUNT(pkg->groups));
	case PKG_SHLIBS_REQUIRED:
		return (HASH_COUNT(pkg->shlibs_required));
	case PKG_SHLIBS_PROVIDED:
		return (HASH_COUNT(pkg->shlibs_provided));
	case PKG_ANNOTATIONS:
		return (HASH_COUNT(pkg->annotations));
	}
	
	return (0);
}

void
pkg_list_free(struct pkg *pkg, pkg_list list)  {
	switch (list) {
	case PKG_DEPS:
		HASH_FREE(pkg->deps, pkg_dep, pkg_dep_free);
		pkg->flags &= ~PKG_LOAD_DEPS;
		break;
	case PKG_RDEPS:
		HASH_FREE(pkg->rdeps, pkg_dep, pkg_dep_free);
		pkg->flags &= ~PKG_LOAD_RDEPS;
		break;
	case PKG_LICENSES:
		HASH_FREE(pkg->licenses, pkg_license, pkg_license_free);
		pkg->flags &= ~PKG_LOAD_LICENSES;
		break;
	case PKG_OPTIONS:
		HASH_FREE(pkg->options, pkg_option, pkg_option_free);
		pkg->flags &= ~PKG_LOAD_OPTIONS;
		break;
	case PKG_CATEGORIES:
		HASH_FREE(pkg->categories, pkg_category, free);
		pkg->flags &= ~PKG_LOAD_CATEGORIES;
		break;
	case PKG_FILES:
		HASH_FREE(pkg->files, pkg_file, pkg_file_free);
		pkg->flags &= ~PKG_LOAD_FILES;
		break;
	case PKG_DIRS:
		HASH_FREE(pkg->dirs, pkg_dir, pkg_dir_free);
		pkg->flags &= ~PKG_LOAD_DIRS;
		break;
	case PKG_USERS:
		HASH_FREE(pkg->users, pkg_user, pkg_user_free);
		pkg->flags &= ~PKG_LOAD_USERS;
		break;
	case PKG_GROUPS:
		HASH_FREE(pkg->groups, pkg_group, pkg_group_free);
		pkg->flags &= ~PKG_LOAD_GROUPS;
		break;
	case PKG_SHLIBS_REQUIRED:
		HASH_FREE(pkg->shlibs_required, pkg_shlib, pkg_shlib_free);
		pkg->flags &= ~PKG_LOAD_SHLIBS_REQUIRED;
		break;
	case PKG_SHLIBS_PROVIDED:
		HASH_FREE(pkg->shlibs_provided, pkg_shlib, pkg_shlib_free);
		pkg->flags &= ~PKG_LOAD_SHLIBS_PROVIDED;
		break;
	case PKG_ANNOTATIONS:
		HASH_FREE(pkg->annotations, pkg_note, pkg_annotation_free);
		pkg->flags &= ~PKG_LOAD_ANNOTATIONS;
		break;
	}
}

int
pkg_open(struct pkg **pkg_p, const char *path, struct pkg_manifest_key *keys, int flags)
{
	struct archive *a;
	struct archive_entry *ae;
	int ret;

	ret = pkg_open2(pkg_p, &a, &ae, path, keys, flags);

	if (ret != EPKG_OK && ret != EPKG_END)
		return (EPKG_FATAL);

	archive_read_free(a);

	return (EPKG_OK);
}

int
pkg_open2(struct pkg **pkg_p, struct archive **a, struct archive_entry **ae,
		const char *path, struct pkg_manifest_key *keys, int flags)
{
	struct pkg *pkg;
	pkg_error_t retcode = EPKG_OK;
	int ret;
	const char *fpath;
	bool manifest = false;
	const void *buf;
	size_t size;
	off_t offset = 0;
	struct sbuf **sbuf;
	int i, r;

	struct {
		const char *name;
		pkg_attr attr;
	} files[] = {
		{ "+MTREE_DIRS", PKG_MTREE },
		{ NULL, 0 }
	};

	assert(path != NULL && path[0] != '\0');

	*a = archive_read_new();
	archive_read_support_filter_all(*a);
	archive_read_support_format_tar(*a);

	if (archive_read_open_filename(*a, path, 4096) != ARCHIVE_OK) {
		pkg_emit_error("archive_read_open_filename(%s): %s", path,
				   archive_error_string(*a));
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (*pkg_p == NULL) {
		retcode = pkg_new(pkg_p, PKG_FILE);
		if (retcode != EPKG_OK)
			goto cleanup;
	} else
		pkg_reset(*pkg_p, PKG_FILE);

	pkg = *pkg_p;
	pkg->type = PKG_FILE;

	while ((ret = archive_read_next_header(*a, ae)) == ARCHIVE_OK) {
		fpath = archive_entry_pathname(*ae);
		if (fpath[0] != '+')
			break;

		if (!manifest &&
			(flags & PKG_OPEN_MANIFEST_COMPACT) &&
			strcmp(fpath, "+COMPACT_MANIFEST") == 0) {
			manifest = true;

			ret = pkg_parse_manifest_archive(pkg, *a, keys);
			if (ret != EPKG_OK) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			/* Do not read anything more */
			break;
		}
		if (!manifest && strcmp(fpath, "+MANIFEST") == 0) {
			manifest = true;

			ret = pkg_parse_manifest_archive(pkg, *a, keys);
			if (ret != EPKG_OK) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			if (flags & PKG_OPEN_MANIFEST_ONLY)
				break;
		}

		for (i = 0; files[i].name != NULL; i++) {
			if (strcmp(fpath, files[i].name) == 0) {
				sbuf = &pkg->fields[files[i].attr];
				sbuf_init(sbuf);
				offset = 0;
				for (;;) {
					if ((r = archive_read_data_block(*a, &buf,
							&size, &offset)) == 0) {
						sbuf_bcat(*sbuf, buf, size);
					}
					else {
						if (r == ARCHIVE_FATAL) {
							retcode = EPKG_FATAL;
							pkg_emit_error("%s is not a valid package: "
									"%s is corrupted: %s", path, fpath,
									archive_error_string(*a));
							goto cleanup;
						}
						else if (r == ARCHIVE_EOF)
							break;
					}
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

	if (!manifest) {
		retcode = EPKG_FATAL;
		pkg_emit_error("%s is not a valid package: no manifest found", path);
	}

	cleanup:
	if (retcode != EPKG_OK && retcode != EPKG_END) {
		if (*a != NULL)
			archive_read_free(*a);
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
		packing_append_file(pack, spath, dpath);
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		snprintf(spath, sizeof(spath), "%s%s", src, pkg_file_path(file));
		snprintf(dpath, sizeof(dpath), "%s%s", dest, pkg_file_path(file));
		packing_append_file(pack, spath, dpath);
	}


	return (packing_finish(pack));
}

int
pkg_test_filesum(struct pkg *pkg)
{
	struct pkg_file *f = NULL;
	const char *path;
	const char *sum;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	int rc = EPKG_OK;

	assert(pkg != NULL);

	while (pkg_files(pkg, &f) == EPKG_OK) {
		path = pkg_file_path(f);
		sum = pkg_file_cksum(f);
		if (*sum != '\0') {
			sha256_file(path, sha256);
			if (strcmp(sha256, sum) != 0) {
				pkg_emit_file_mismatch(pkg, f, sum);
				rc = EPKG_FATAL;
			}
		}
	}

	return (rc);
}

int
pkg_recompute(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_file *f = NULL;
	const char *path;
	struct hardlinks *hl = NULL;
	int64_t flatsize = 0;
	int64_t oldflatsize;
	struct stat st;
	bool regular = false;
	const char *sum;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	int rc = EPKG_OK;

	while (pkg_files(pkg, &f) == EPKG_OK) {
		path = pkg_file_path(f);
		sum = pkg_file_cksum(f);
		if (lstat(path, &st) == 0) {
			regular = true;
			if (S_ISLNK(st.st_mode)) {
				regular = false;
				*sha256 = '\0';
			} else {
				if (sha256_file(path, sha256) != EPKG_OK) {
					rc = EPKG_FATAL;
					break;
				}
			}

			/* special case for hardlinks */
			if (st.st_nlink > 1)
				regular = is_hardlink(hl, &st);

			if (regular)
				flatsize += st.st_size;
		}
		if (strcmp(sha256, sum) != 0)
			pkgdb_file_set_cksum(db, f, sha256);
	}

	pkg_get(pkg, PKG_FLATSIZE, &oldflatsize);
	if (flatsize != oldflatsize)
		pkgdb_set(db, pkg, PKG_SET_FLATSIZE, flatsize);

	return (rc);
}

int
pkg_try_installed(struct pkgdb *db, const char *origin,
		struct pkg **pkg, unsigned flags) {
	struct pkgdb_it *it = NULL;
	int ret = EPKG_FATAL;

	if ((it = pkgdb_query(db, origin, MATCH_EXACT)) == NULL)
		return (EPKG_FATAL);

	ret = pkgdb_it_next(it, pkg, flags);
	pkgdb_it_free(it);

	return (ret);
}

int
pkg_is_installed(struct pkgdb *db, const char *origin)
{
	struct pkg *pkg = NULL;
	int ret = EPKG_FATAL;

	ret = pkg_try_installed(db, origin, &pkg, PKG_LOAD_BASIC);
	pkg_free(pkg);

	return (ret);
}

bool
pkg_has_file(struct pkg *p, const char *path)
{
	struct pkg_file *f;

	HASH_FIND_STR(p->files, __DECONST(char *, path), f);

	return (f != NULL ? true : false);
}

bool
pkg_has_dir(struct pkg *p, const char *path)
{
	struct pkg_dir *d;

	HASH_FIND_STR(p->dirs, __DECONST(char *, path), d);

	return (d != NULL ? true : false);
}
