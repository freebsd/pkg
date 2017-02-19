/*-
 * Copyright (c) 2011-2016 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
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
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"

int
pkg_new(struct pkg **pkg, pkg_t type)
{
	*pkg = xcalloc(1, sizeof(struct pkg));
	(*pkg)->type = type;
	(*pkg)->rootfd = -1;

	return (EPKG_OK);
}

static void
pkg_message_free(struct pkg_message *m)
{
	free(m->str);
	free(m->maximum_version);
	free(m->minimum_version);
	free(m);
}

void
pkg_free(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	free(pkg->name);
	free(pkg->origin);
	free(pkg->old_version);
	free(pkg->maintainer);
	free(pkg->www);
	free(pkg->arch);
	free(pkg->abi);
	free(pkg->uid);
	free(pkg->digest);
	free(pkg->old_digest);
	free(pkg->prefix);
	free(pkg->comment);
	free(pkg->desc);
	free(pkg->sum);
	free(pkg->repopath);
	free(pkg->repourl);
	free(pkg->reason);
	free(pkg->dep_formula);

	for (int i = 0; i < PKG_NUM_SCRIPTS; i++)
		if (pkg->scripts[i])
			utstring_free(pkg->scripts[i]);

	pkg_list_free(pkg, PKG_DEPS);
	pkg_list_free(pkg, PKG_RDEPS);
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg_list_free(pkg, PKG_OPTIONS);
	pkg_list_free(pkg, PKG_USERS);
	pkg_list_free(pkg, PKG_GROUPS);
	pkg_list_free(pkg, PKG_SHLIBS_REQUIRED);
	pkg_list_free(pkg, PKG_SHLIBS_PROVIDED);
	pkg_list_free(pkg, PKG_PROVIDES);
	pkg_list_free(pkg, PKG_REQUIRES);
	pkg_list_free(pkg, PKG_CATEGORIES);
	pkg_list_free(pkg, PKG_LICENSES);

	DL_FREE(pkg->message, pkg_message_free);
	DL_FREE(pkg->annotations, pkg_kv_free);

	if (pkg->rootfd != -1)
		close(pkg->rootfd);

	free(pkg);
}

pkg_t
pkg_type(const struct pkg * restrict pkg)
{
	assert(pkg != NULL);

	return (pkg->type);
}

int
pkg_is_valid(const struct pkg * restrict pkg)
{
	if (pkg == NULL) {
		pkg_emit_error("Invalid package: not allocated");
		return (EPKG_FATAL);
	}

	if (pkg->origin == NULL) {
		pkg_emit_error("Invalid package: object has missing property origin");
		return (EPKG_FATAL);
	}

	if (pkg->name == NULL) {
		pkg_emit_error("Invalid package: object has missing property name");
		return (EPKG_FATAL);
	}

	if (pkg->comment == NULL) {
		pkg_emit_error("Invalid package: object has missing property comment");
		return (EPKG_FATAL);
	}

	if (pkg->version == NULL) {
		pkg_emit_error("Invalid package: object has missing property version");
		return (EPKG_FATAL);
	}

	if (pkg->desc == NULL) {
		pkg_emit_error("Invalid package: object has missing property desc");
		return (EPKG_FATAL);
	}

	if (pkg->maintainer == NULL) {
		pkg_emit_error("Invalid package: object has missing property maintainer");
		return (EPKG_FATAL);
	}

	if (pkg->www == NULL) {
		pkg_emit_error("Invalid package: object has missing property www");
		return (EPKG_FATAL);
	}

	if (pkg->prefix == NULL) {
		pkg_emit_error("Invalid package: object has missing property prefix");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
pkg_vget(const struct pkg * restrict pkg, va_list ap)
{
	int attr;

	while ((attr = va_arg(ap, int)) > 0) {

		if (attr >= PKG_NUM_FIELDS || attr <= 0) {
			pkg_emit_error("Bad argument on pkg_get %d", attr);
			return (EPKG_FATAL);
		}

		switch (attr) {
		case PKG_ORIGIN:
			*va_arg(ap, const char **) = pkg->origin;
			break;
		case PKG_NAME:
			*va_arg(ap, const char **) = pkg->name;
			break;
		case PKG_VERSION:
			*va_arg(ap, const char **) = pkg->version;
			break;
		case PKG_COMMENT:
			*va_arg(ap, const char **) = pkg->comment;
			break;
		case PKG_DESC:
			*va_arg(ap, const char **) = pkg->desc;
			break;
		case PKG_MTREE:
			*va_arg(ap, const char **) = NULL;
			break;
		case PKG_MESSAGE:
			*va_arg(ap, const char **) = pkg->message ? pkg->message->str : NULL;
			break;
		case PKG_ARCH:
			*va_arg(ap, const char **) = pkg->arch;
			break;
		case PKG_ABI:
			*va_arg(ap, const char **) = pkg->abi;
			break;
		case PKG_WWW:
			*va_arg(ap, const char **) = pkg->www;
			break;
		case PKG_MAINTAINER:
			*va_arg(ap, const char **) = pkg->maintainer;
			break;
		case PKG_PREFIX:
			*va_arg(ap, const char **) = pkg->prefix;
			break;
		case PKG_REPOPATH:
			*va_arg(ap, const char **) = pkg->repopath;
			break;
		case PKG_CKSUM:
			*va_arg(ap, const char **) = pkg->sum;
			break;
		case PKG_OLD_VERSION:
			*va_arg(ap, const char **) = pkg->old_version;
			break;
		case PKG_REPONAME:
			*va_arg(ap, const char **) = pkg->reponame;
			break;
		case PKG_REPOURL:
			*va_arg(ap, const char **) = pkg->repourl;
			break;
		case PKG_DIGEST:
			*va_arg(ap, const char **) = pkg->digest;
			break;
		case PKG_REASON:
			*va_arg(ap, const char **) = pkg->reason;
			break;
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
		case PKG_ROWID:
			*va_arg(ap, int64_t *) = pkg->id;
			break;
		case PKG_TIME:
			*va_arg(ap, int64_t *) = pkg->timestamp;
			break;
		case PKG_ANNOTATIONS:
			*va_arg(ap, const struct pkg_kv **) = pkg->annotations;
			break;
		case PKG_UNIQUEID:
			*va_arg(ap, const char **) = pkg->uid;
			break;
		case PKG_OLD_DIGEST:
			*va_arg(ap, const char **) = pkg->old_digest;
			break;
		case PKG_DEP_FORMULA:
			*va_arg(ap, const char **) = pkg->dep_formula;
			break;
		case PKG_VITAL:
			*va_arg(ap, bool *) = pkg->vital;
			break;
		}
	}

	return (EPKG_OK);
}

int
pkg_get2(const struct pkg * restrict pkg, ...)
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
	int attr;
	const char *buf;
	ucl_object_t *obj;
	struct pkg_message *msg;

	while ((attr = va_arg(ap, int)) > 0) {
		if (attr >= PKG_NUM_FIELDS || attr <= 0) {
			pkg_emit_error("Bad argument on pkg_set %d", attr);
			return (EPKG_FATAL);
		}

		switch (attr) {
		case PKG_NAME:
			free(pkg->name);
			pkg->name = xstrdup(va_arg(ap, const char *));
			free(pkg->uid);
			pkg->uid = xstrdup(pkg->name);
			break;
		case PKG_ORIGIN:
			free(pkg->origin);
			pkg->origin = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_VERSION:
			free(pkg->version);
			pkg->version = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_COMMENT:
			free(pkg->comment);
			pkg->comment = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_DESC:
			free(pkg->desc);
			pkg->desc = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_MTREE:
			(void)va_arg(ap, const char *);
			break;
		case PKG_MESSAGE:
			LL_FOREACH(pkg->message, msg) {
				pkg_message_free(msg);
			}
			buf = va_arg(ap, const char *);
			if (*buf == '[') {
				pkg_message_from_str(pkg, buf, strlen(buf));
			} else {
				obj = ucl_object_fromstring_common(buf, strlen(buf),
				    UCL_STRING_RAW|UCL_STRING_TRIM);
				pkg_message_from_ucl(pkg, obj);
				ucl_object_unref(obj);
			}
			break;
		case PKG_ARCH:
			free(pkg->arch);
			pkg->arch = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_ABI:
			free(pkg->abi);
			pkg->abi = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_MAINTAINER:
			free(pkg->maintainer);
			pkg->maintainer = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_WWW:
			free(pkg->www);
			pkg->www = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_PREFIX:
			free(pkg->prefix);
			pkg->prefix = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_REPOPATH:
			free(pkg->repopath);
			pkg->repopath = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_CKSUM:
			free(pkg->sum);
			pkg->sum = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_OLD_VERSION:
			free(pkg->old_version);
			pkg->old_version = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_REPONAME:
			free(pkg->reponame);
			pkg->reponame = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_REPOURL:
			free(pkg->repourl);
			pkg->repourl = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_DIGEST:
			free(pkg->digest);
			pkg->digest = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_REASON:
			free(pkg->reason);
			pkg->reason = xstrdup(va_arg(ap, const char *));
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
		case PKG_LICENSE_LOGIC:
			pkg->licenselogic = (lic_t)va_arg(ap, int);
			break;
		case PKG_AUTOMATIC:
			pkg->automatic = (bool)va_arg(ap, int);
			break;
		case PKG_ROWID:
			pkg->id = va_arg(ap, int64_t);
			break;
		case PKG_LOCKED:
			pkg->locked = (bool)va_arg(ap, int);
			break;
		case PKG_TIME:
			pkg->timestamp = va_arg(ap, int64_t);
			break;
		case PKG_DEP_FORMULA:
			free(pkg->dep_formula);
			pkg->dep_formula = xstrdup(va_arg(ap, const char *));
			break;
		case PKG_VITAL:
			pkg->vital = (bool)va_arg(ap, int);
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
pkg_set_from_fileat(int fd, struct pkg *pkg, pkg_attr attr, const char *path,
    bool trimcr)
{
	char *buf = NULL;
	char *cp;
	off_t size = 0;
	int ret = EPKG_OK;

	assert(pkg != NULL);
	assert(path != NULL);

	if ((ret = file_to_bufferat(fd, path, &buf, &size)) !=  EPKG_OK)
		return (ret);

	if (trimcr) {
		cp = buf + strlen(buf) - 1;
		while (cp > buf && *cp == '\n') {
			*cp = 0;
			cp--;
		}
	}

	ret = pkg_set(pkg, attr, buf);

	free(buf);

	return (ret);
}

int
pkg_set_from_file(struct pkg *pkg, pkg_attr attr, const char *path, bool trimcr)
{
	char *buf = NULL;
	char *cp;
	off_t size = 0;
	int ret = EPKG_OK;

	assert(pkg != NULL);
	assert(path != NULL);

	if ((ret = file_to_buffer(path, &buf, &size)) !=  EPKG_OK)
		return (ret);

	if (trimcr) {
		cp = buf + strlen(buf) - 1;
		while (cp > buf && *cp == '\n') {
			*cp = 0;
			cp--;
		}
	}

	ret = pkg_set(pkg, attr, buf);

	free(buf);

	return (ret);
}

#define pkg_each(name, type, field)		\
int						\
pkg_##name(const struct pkg *p, type **t) {	\
	assert(p != NULL);			\
	if ((*t) == NULL)			\
		(*t) = p->field;		\
	else					\
		(*t) = (*t)->next;		\
	if ((*t) == NULL)			\
		return (EPKG_END);		\
	return (EPKG_OK);			\
}

pkg_each(dirs, struct pkg_dir, dirs);
pkg_each(files, struct pkg_file, files);
pkg_each(deps, struct pkg_dep, depends);
pkg_each(rdeps, struct pkg_dep, rdepends);
pkg_each(options, struct pkg_option, options);
pkg_each(conflicts, struct pkg_conflict, conflicts);

#define pkg_each_hash(name, htype, type, attrib)	\
int							\
pkg_##name(const struct pkg *pkg, type **c) {		\
	assert(pkg != NULL);				\
	kh_next(htype, pkg->name, (*c), attrib);	\
}
pkg_each_hash(config_files, pkg_config_files, struct pkg_config_file, path);

#define pkg_each_strings(name)			\
int						\
pkg_##name(const struct pkg *pkg, char **c) {	\
	assert(pkg != NULL);			\
	kh_string_next(pkg->name, (*c));	\
}

pkg_each_strings(categories);
pkg_each_strings(licenses);
pkg_each_strings(requires);
pkg_each_strings(provides);
pkg_each_strings(shlibs_required);
pkg_each_strings(shlibs_provided);
pkg_each_strings(users);
pkg_each_strings(groups);

int
pkg_adduser(struct pkg *pkg, const char *name)
{
	char *storename;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	if (kh_contains(strings, pkg->users, name)) {
		if (developer_mode) {
			pkg_emit_error("duplicate user listing: %s, fatal (developer mode)", name);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate user listing: %s, ignoring", name);
			return (EPKG_OK);
		}
	}

	storename = xstrdup(name);
	kh_add(strings, pkg->users, storename, storename, free);

	return (EPKG_OK);
}

int
pkg_addgroup(struct pkg *pkg, const char *name)
{
	char *storename;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	if (kh_contains(strings, pkg->groups, name)) {
		if (developer_mode) {
			pkg_emit_error("duplicate group listing: %s, fatal (developer mode)", name);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate group listing: %s, ignoring", name);
			return (EPKG_OK);
		}
	}

	storename = xstrdup(name);
	kh_add(strings, pkg->groups, storename, storename, free);

	return (EPKG_OK);
}

int
pkg_adddep(struct pkg *pkg, const char *name, const char *origin, const char *version, bool locked)
{
	struct pkg_dep *d = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	assert(origin != NULL && origin[0] != '\0');

	pkg_debug(3, "Pkg: add a new dependency origin: %s, name: %s", origin, name);
	if (kh_contains(pkg_deps, pkg->depshash, name)) {
		if (developer_mode) {
			pkg_emit_error("%s: duplicate dependency listing: %s, fatal (developer mode)",
			    pkg->name, name);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("%s-%s: duplicate dependency listing: %s, ignoring",
			    pkg->name, pkg->version, name);
			return (EPKG_OK);
		}
	}

	d = xcalloc(1, sizeof(*d));
	d->origin = xstrdup(origin);
	d->name = xstrdup(name);
	if (version != NULL && version[0] != '\0')
		d->version = xstrdup(version);
	d->uid = xstrdup(name);
	d->locked = locked;

	kh_add(pkg_deps, pkg->depshash, d, d->name, pkg_dep_free);
	LL_APPEND(pkg->depends, d);

	return (EPKG_OK);
}

int
pkg_addrdep(struct pkg *pkg, const char *name, const char *origin, const char *version, bool locked)
{
	struct pkg_dep *d;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	assert(origin != NULL && origin[0] != '\0');

	pkg_debug(3, "Pkg: add a new reverse dependency origin: %s, name: %s", origin, name);

	d = xcalloc(1, sizeof(*d));
	d->origin = xstrdup(origin);
	d->name = xstrdup(name);
	if (version != NULL && version[0] != '\0')
		d->version = xstrdup(version);
	d->uid = xstrdup(name);
	d->locked = locked;

	kh_add(pkg_deps, pkg->rdepshash, d, d->name, pkg_dep_free);
	LL_APPEND(pkg->rdepends, d);

	return (EPKG_OK);
}

int
pkg_addfile(struct pkg *pkg, const char *path, const char *sum, bool check_duplicates)
{
	return (pkg_addfile_attr(pkg, path, sum, NULL, NULL, 0, 0, check_duplicates));
}

int
pkg_addfile_attr(struct pkg *pkg, const char *path, const char *sum,
    const char *uname, const char *gname, mode_t perm, u_long fflags,
    bool check_duplicates)
{
	struct pkg_file *f = NULL;
	char abspath[MAXPATHLEN];

	assert(pkg != NULL);
	assert(path != NULL && path[0] != '\0');

	path = pkg_absolutepath(path, abspath, sizeof(abspath), false);
	pkg_debug(3, "Pkg: add new file '%s'", path);

	if (check_duplicates && kh_contains(pkg_files, pkg->filehash, path)) {
		if (developer_mode) {
			pkg_emit_error("duplicate file listing: %s, fatal (developer mode)", path);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate file listing: %s, ignoring", path);
			return (EPKG_OK);
		}
	}

	f = xcalloc(1, sizeof(*f));
	strlcpy(f->path, path, sizeof(f->path));

	if (sum != NULL)
		f->sum = xstrdup(sum);

	if (uname != NULL)
		strlcpy(f->uname, uname, sizeof(f->uname));

	if (gname != NULL)
		strlcpy(f->gname, gname, sizeof(f->gname));

	if (perm != 0)
		f->perm = perm;

	if (fflags != 0)
		f->fflags = fflags;

	kh_safe_add(pkg_files, pkg->filehash, f, f->path);
	DL_APPEND(pkg->files, f);

	return (EPKG_OK);
}

int
pkg_addconfig_file(struct pkg *pkg, const char *path, const char *content)
{
	struct pkg_config_file *f = NULL;
	char abspath[MAXPATHLEN];

	path = pkg_absolutepath(path, abspath, sizeof(abspath), false);
	pkg_debug(3, "Pkg: add new config file '%s'", path);

	if (kh_contains(pkg_config_files, pkg->config_files, path)) {
		if (developer_mode) {
			pkg_emit_error("duplicate file listing: %s, fatal (developer mode)", path);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate file listing: %s, ignoring", path);
		}
	}
	f = xcalloc(1, sizeof(*f));
	strlcpy(f->path, path, sizeof(f->path));

	if (content != NULL)
		f->content = xstrdup(content);

	kh_add(pkg_config_files, pkg->config_files, f, f->path, pkg_config_file_free);

	return (EPKG_OK);
}

int
pkg_addstring(kh_strings_t **list, const char *val, const char *title)
{
	char *store;

	assert(val != NULL);
	assert(title != NULL);

	if (kh_contains(strings, *list, val)) {
		if (developer_mode) {
			pkg_emit_error("duplicate %s listing: %s, fatal"
			    " (developer mode)", title, val);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate %s listing: %s, "
			    "ignoring", title, val);
			return (EPKG_OK);
		}
	}

	store = xstrdup(val);
	kh_add(strings, *list, store, store, free);

	return (EPKG_OK);
}

int
pkg_adddir(struct pkg *pkg, const char *path, bool check_duplicates)
{
	return(pkg_adddir_attr(pkg, path, NULL, NULL, 0, 0, check_duplicates));
}

int
pkg_adddir_attr(struct pkg *pkg, const char *path, const char *uname,
    const char *gname, mode_t perm, u_long fflags, bool check_duplicates)
{
	struct pkg_dir *d = NULL;
	char abspath[MAXPATHLEN];

	assert(pkg != NULL);
	assert(path != NULL && path[0] != '\0');

	if (strcmp(path, "/") == 0) {
		pkg_emit_error("skipping useless directory: '%s'\n", path);
		return (EPKG_OK);
	}
	path = pkg_absolutepath(path, abspath, sizeof(abspath), false);
	pkg_debug(3, "Pkg: add new directory '%s'", path);
	if (check_duplicates && kh_contains(pkg_dirs, pkg->dirhash, path)) {
		if (developer_mode) {
			pkg_emit_error("duplicate directory listing: %s, fatal (developer mode)", path);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate directory listing: %s, ignoring", path);
			return (EPKG_OK);
		}
	}

	d = xcalloc(1, sizeof(*d));
	strlcpy(d->path, path, sizeof(d->path));

	if (uname != NULL)
		strlcpy(d->uname, uname, sizeof(d->uname));

	if (gname != NULL)
		strlcpy(d->gname, gname, sizeof(d->gname));

	if (perm != 0)
		d->perm = perm;

	if (fflags != 0)
		d->fflags = fflags;

	kh_safe_add(pkg_dirs, pkg->dirhash, d, d->path);
	DL_APPEND(pkg->dirs, d);

	return (EPKG_OK);
}

int
pkg_addscript(struct pkg *pkg, const char *data, pkg_script type)
{

	assert(pkg != NULL);
	utstring_renew(pkg->scripts[type]);
	utstring_printf(pkg->scripts[type], "%s", data);

	return (EPKG_OK);
}

int
pkg_addscript_fileat(int fd, struct pkg *pkg, const char *filename)
{
	char *data;
	pkg_script type;
	int ret = EPKG_OK;
	off_t sz = 0;

	assert(pkg != NULL);
	assert(filename != NULL);

	pkg_debug(1, "Adding script from: '%s'", filename);

	if ((ret = file_to_bufferat(fd, filename, &data, &sz)) != EPKG_OK)
		return (ret);

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
		ret = EPKG_FATAL;
		goto cleanup;
	}

	ret = pkg_addscript(pkg, data, type);
cleanup:
	free(data);
	return (ret);
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

	pkg_debug(1, "Adding script from: '%s'", path);

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
		ret = EPKG_FATAL;
		goto cleanup;
	}

	ret = pkg_addscript(pkg, data, type);
cleanup:
	free(data);
	return (ret);
}

int
pkg_appendscript(struct pkg *pkg, const char *cmd, pkg_script type)
{

	assert(pkg != NULL);
	assert(cmd != NULL && cmd[0] != '\0');

	if (pkg->scripts[type] == NULL)
		utstring_new(pkg->scripts[type]);

	utstring_printf(pkg->scripts[type], "%s", cmd);

	return (EPKG_OK);
}

int
pkg_addoption(struct pkg *pkg, const char *key, const char *value)
{
	struct pkg_option	*o = NULL;

	assert(pkg != NULL);
	assert(key != NULL && key[0] != '\0');
	assert(value != NULL && value[0] != '\0');

	/* There might be a default or description for the option
	   already, so we only count it as a duplicate if the value
	   field is already set. Which implies there could be a
	   default value or description for an option but no actual
	   value. */

	pkg_debug(2,"Pkg> adding options: %s = %s", key, value);
	if (kh_contains(pkg_options, pkg->optionshash, key)) {
		if (developer_mode) {
			pkg_emit_error("duplicate options listing: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate options listing: %s, ignoring", key);
			return (EPKG_OK);
		}
	}
	o = xcalloc(1, sizeof(*o));
	o->key = xstrdup(key);
	o->value = xstrdup(value);
	kh_safe_add(pkg_options, pkg->optionshash, o, o->key);
	DL_APPEND(pkg->options, o);

	return (EPKG_OK);
}

int
pkg_addoption_default(struct pkg *pkg, const char *key,
		      const char *default_value)
{
	struct pkg_option *o = NULL;

	assert(pkg != NULL);
	assert(key != NULL && key[0] != '\0');
	assert(default_value != NULL && default_value[0] != '\0');

	/* There might be a value or description for the option
	   already, so we only count it as a duplicate if the
	   default_value field is already set. Which implies there
	   could be a default value or description for an option but
	   no actual value. */

	if (kh_contains(pkg_options, pkg->optionshash, key)) {
		if (developer_mode) {
			pkg_emit_error("duplicate default value for option: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate default value for option: %s, ignoring", key);
			return (EPKG_OK);
		}
	}
	o = xcalloc(1, sizeof(*o));
	o->key = xstrdup(key);
	o->default_value = xstrdup(default_value);
	kh_safe_add(pkg_options, pkg->optionshash, o, o->key);
	DL_APPEND(pkg->options, o);

	return (EPKG_OK);
}

int
pkg_addoption_description(struct pkg *pkg, const char *key,
			  const char *description)
{
	struct pkg_option *o = NULL;

	assert(pkg != NULL);
	assert(key != NULL && key[0] != '\0');
	assert(description != NULL && description[0] != '\0');

	/* There might be a value or default for the option already,
	   so we only count it as a duplicate if the description field
	   is already set. Which implies there could be a default
	   value or description for an option but no actual value. */

	if (kh_contains(pkg_options, pkg->optionshash, key)) {
		if (developer_mode) {
			pkg_emit_error("duplicate description for option: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate description for option: %s, ignoring", key);
			return (EPKG_OK);
		}
	}

	o = xcalloc(1, sizeof(*o));
	o->key = xstrdup(key);
	o->description = xstrdup(description);
	kh_safe_add(pkg_options, pkg->optionshash, o, o->key);
	DL_APPEND(pkg->options, o);

	return (EPKG_OK);
}

int
pkg_addshlib_required(struct pkg *pkg, const char *name)
{
	char *storename;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	/* silently ignore duplicates in case of shlibs */
	if (kh_contains(strings, pkg->shlibs_required, name))
		return (EPKG_OK);

	storename = xstrdup(name);
	kh_add(strings, pkg->shlibs_required, storename, storename, free);

	pkg_debug(3, "added shlib deps for %s on %s", pkg->name, name);

	return (EPKG_OK);
}

int
pkg_addshlib_provided(struct pkg *pkg, const char *name)
{
	char *storename;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	/* ignore files which are not starting with lib */
	if (strncmp(name, "lib", 3) != 0)
		return (EPKG_OK);

	/* silently ignore duplicates in case of shlibs */
	if (kh_contains(strings, pkg->shlibs_provided, name))
		return (EPKG_OK);

	storename = xstrdup(name);
	kh_add(strings, pkg->shlibs_provided, storename, storename, free);

	pkg_debug(3, "added shlib provide %s for %s", name, pkg->name);

	return (EPKG_OK);
}

int
pkg_addconflict(struct pkg *pkg, const char *uniqueid)
{
	struct pkg_conflict *c = NULL;

	assert(pkg != NULL);
	assert(uniqueid != NULL && uniqueid[0] != '\0');

	if (kh_contains(pkg_conflicts, pkg->conflictshash, uniqueid)) {
		/* silently ignore duplicates in case of conflicts */
		return (EPKG_OK);
	}

	c = xcalloc(1, sizeof(*c));
	c->uid = xstrdup(uniqueid);
	pkg_debug(3, "Pkg: add a new conflict origin: %s, with %s", pkg->uid, uniqueid);

	kh_safe_add(pkg_conflicts, pkg->conflictshash, c, c->uid);
	DL_APPEND(pkg->conflicts, c);

	return (EPKG_OK);
}

int
pkg_addrequire(struct pkg *pkg, const char *name)
{
	char *storename;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	/* silently ignore duplicates in case of conflicts */
	if (kh_contains(strings, pkg->requires, name))
		return (EPKG_OK);

	storename = xstrdup(name);

	kh_add(strings, pkg->requires, storename, storename, free);

	return (EPKG_OK);
}

int
pkg_addprovide(struct pkg *pkg, const char *name)
{
	char *storename;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	/* silently ignore duplicates in case of conflicts */
	if (kh_contains(strings, pkg->provides, name))
		return (EPKG_OK);

	storename = xstrdup(name);

	kh_add(strings, pkg->provides, storename, storename, free);

	return (EPKG_OK);
}

const char *
pkg_kv_get(struct pkg_kv *const *kv, const char *tag)
{
	struct pkg_kv *k;

	assert(tag != NULL);

	LL_FOREACH(*kv, k) {
		if (strcmp(k->key, tag) == 0)
			return (k->value);
	}

	return (NULL);
}

int
pkg_kv_add(struct pkg_kv **list, const char *key, const char *val, const char *title)
{
	struct pkg_kv *kv;

	assert(val != NULL);
	assert(title != NULL);

	LL_FOREACH(*list, kv) {
		if (strcmp(kv->key, key) == 0) {
			if (developer_mode) {
				pkg_emit_error("duplicate %s: %s, fatal"
				    " (developer mode)", title, key);
				return (EPKG_FATAL);
			} else {
				pkg_emit_error("duplicate %s: %s, "
				    "ignoring", title, val);
				return (EPKG_OK);
			}
		}
	}

	kv = pkg_kv_new(key, val);
	DL_APPEND(*list, kv);

	return (EPKG_OK);
}

int
pkg_list_count(const struct pkg *pkg, pkg_list list)
{
	switch (list) {
	case PKG_DEPS:
		return (kh_count(pkg->depshash));
	case PKG_RDEPS:
		return (kh_count(pkg->rdepshash));
	case PKG_OPTIONS:
		return (kh_count(pkg->optionshash));
	case PKG_FILES:
		return (kh_count(pkg->filehash));
	case PKG_DIRS:
		return (kh_count(pkg->dirhash));
	case PKG_USERS:
		return (kh_count(pkg->users));
	case PKG_GROUPS:
		return (kh_count(pkg->groups));
	case PKG_SHLIBS_REQUIRED:
		return (kh_count(pkg->shlibs_required));
	case PKG_SHLIBS_PROVIDED:
		return (kh_count(pkg->shlibs_provided));
	case PKG_CONFLICTS:
		return (kh_count(pkg->conflictshash));
	case PKG_PROVIDES:
		return (kh_count(pkg->provides));
	case PKG_REQUIRES:
		return (kh_count(pkg->requires));
	case PKG_CONFIG_FILES:
		return (kh_count(pkg->config_files));
	case PKG_CATEGORIES:
		return (kh_count(pkg->categories));
	case PKG_LICENSES:
		return (kh_count(pkg->licenses));
	}
	
	return (0);
}

void
pkg_list_free(struct pkg *pkg, pkg_list list)  {
	switch (list) {
	case PKG_DEPS:
		LL_FREE(pkg->depends, pkg_dep_free);
		kh_destroy_pkg_deps(pkg->depshash);
		pkg->flags &= ~PKG_LOAD_DEPS;
		break;
	case PKG_RDEPS:
		LL_FREE(pkg->rdepends, pkg_dep_free);
		kh_destroy_pkg_deps(pkg->rdepshash);
		pkg->flags &= ~PKG_LOAD_RDEPS;
		break;
	case PKG_OPTIONS:
		DL_FREE(pkg->options, pkg_option_free);
		kh_destroy_pkg_options(pkg->optionshash);
		pkg->flags &= ~PKG_LOAD_OPTIONS;
		break;
	case PKG_FILES:
	case PKG_CONFIG_FILES:
		DL_FREE(pkg->files, pkg_file_free);
		kh_destroy_pkg_files(pkg->filehash);
		kh_free(pkg_config_files, pkg->config_files, struct pkg_config_file, pkg_config_file_free);
		pkg->flags &= ~PKG_LOAD_FILES;
		break;
	case PKG_DIRS:
		DL_FREE(pkg->dirs, free);
		kh_destroy_pkg_dirs(pkg->dirhash);
		pkg->flags &= ~PKG_LOAD_DIRS;
		break;
	case PKG_USERS:
		kh_free(strings, pkg->users, char, free);
		pkg->flags &= ~PKG_LOAD_USERS;
		break;
	case PKG_GROUPS:
		kh_free(strings, pkg->groups, char, free);
		pkg->flags &= ~PKG_LOAD_GROUPS;
		break;
	case PKG_SHLIBS_REQUIRED:
		kh_free(strings, pkg->shlibs_required, char, free);
		pkg->flags &= ~PKG_LOAD_SHLIBS_REQUIRED;
		break;
	case PKG_SHLIBS_PROVIDED:
		kh_free(strings, pkg->shlibs_provided, char, free);
		pkg->flags &= ~PKG_LOAD_SHLIBS_PROVIDED;
		break;
	case PKG_CONFLICTS:
		DL_FREE(pkg->conflicts, pkg_conflict_free);
		kh_destroy_pkg_conflicts(pkg->conflictshash);
		pkg->flags &= ~PKG_LOAD_CONFLICTS;
		break;
	case PKG_PROVIDES:
		kh_free(strings, pkg->provides, char, free);
		pkg->flags &= ~PKG_LOAD_PROVIDES;
		break;
	case PKG_REQUIRES:
		kh_free(strings, pkg->requires, char, free);
		pkg->flags &= ~PKG_LOAD_REQUIRES;
		break;
	case PKG_CATEGORIES:
		kh_free(strings, pkg->categories, char, free);
		pkg->flags &= ~PKG_LOAD_CATEGORIES;
		break;
	case PKG_LICENSES:
		kh_free(strings, pkg->licenses, char, free);
		pkg->flags &= ~PKG_LOAD_LICENSES;
		break;
	}
}

int
pkg_open(struct pkg **pkg_p, const char *path, struct pkg_manifest_key *keys, int flags)
{
	struct archive *a;
	struct archive_entry *ae;
	int ret;

	ret = pkg_open2(pkg_p, &a, &ae, path, keys, flags, -1);

	if (ret != EPKG_OK && ret != EPKG_END)
		return (EPKG_FATAL);

	archive_read_close(a);
	archive_read_free(a);

	return (EPKG_OK);
}

int
pkg_open_fd(struct pkg **pkg_p, int fd, struct pkg_manifest_key *keys, int flags)
{
	struct archive *a;
	struct archive_entry *ae;
	int ret;

	ret = pkg_open2(pkg_p, &a, &ae, NULL, keys, flags, fd);

	if (ret != EPKG_OK && ret != EPKG_END)
		return (EPKG_FATAL);

	archive_read_close(a);
	archive_read_free(a);

	return (EPKG_OK);
}

int
pkg_open2(struct pkg **pkg_p, struct archive **a, struct archive_entry **ae,
    const char *path, struct pkg_manifest_key *keys, int flags, int fd)
{
	struct pkg	*pkg = NULL;
	pkg_error_t	 retcode = EPKG_OK;
	int		 ret;
	const char	*fpath;
	bool		 manifest = false;
	bool		 read_from_stdin = 0;

	*a = archive_read_new();
	archive_read_support_filter_all(*a);
	archive_read_support_format_tar(*a);

	/* archive_read_open_filename() treats a path of NULL as
	 * meaning "read from stdin," but we want this behaviour if
	 * path is exactly "-". In the unlikely event of wanting to
	 * read an on-disk file called "-", just say "./-" or some
	 * other leading path. */

	if (fd == -1) {
		read_from_stdin = (strncmp(path, "-", 2) == 0);

		if (archive_read_open_filename(*a,
		    read_from_stdin ? NULL : path, 4096) != ARCHIVE_OK) {
			if ((flags & PKG_OPEN_TRY) == 0)
				pkg_emit_error("archive_read_open_filename(%s): %s", path,
					archive_error_string(*a));

			retcode = EPKG_FATAL;
			goto cleanup;
		}
	} else {
		if (archive_read_open_fd(*a, fd, 4096) != ARCHIVE_OK) {
			if ((flags & PKG_OPEN_TRY) == 0)
				pkg_emit_error("archive_read_open_fd: %s",
					archive_error_string(*a));

			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}

	retcode = pkg_new(pkg_p, PKG_FILE);
	if (retcode != EPKG_OK)
		goto cleanup;

	pkg = *pkg_p;

	while ((ret = archive_read_next_header(*a, ae)) == ARCHIVE_OK) {
		fpath = archive_entry_pathname(*ae);
		if (fpath[0] != '+')
			break;

		if (!manifest &&
			(flags & PKG_OPEN_MANIFEST_COMPACT) &&
			strcmp(fpath, "+COMPACT_MANIFEST") == 0) {
			char *buffer;
			manifest = true;

			size_t len = archive_entry_size(*ae);
			buffer = xmalloc(len);
			archive_read_data(*a, buffer, archive_entry_size(*ae));
			ret = pkg_parse_manifest(pkg, buffer, len, keys);
			free(buffer);
			if (ret != EPKG_OK) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			/* Do not read anything more */
			break;
		}
		if (!manifest && strcmp(fpath, "+MANIFEST") == 0) {
			manifest = true;
			char *buffer;

			size_t len = archive_entry_size(*ae);
			buffer = xmalloc(len);
			archive_read_data(*a, buffer, archive_entry_size(*ae));
			ret = pkg_parse_manifest(pkg, buffer, len, keys);
			free(buffer);
			if (ret != EPKG_OK) {
				if ((flags & PKG_OPEN_TRY) == 0)
					pkg_emit_error("%s is not a valid package: "
						"Invalid manifest", path);

				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (flags & PKG_OPEN_MANIFEST_ONLY)
				break;
		}
	}

	if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) {
		if ((flags & PKG_OPEN_TRY) == 0)
			pkg_emit_error("archive_read_next_header(): %s",
				archive_error_string(*a));

		retcode = EPKG_FATAL;
	}

	if (ret == ARCHIVE_EOF)
		retcode = EPKG_END;

	if (!manifest) {
		retcode = EPKG_FATAL;
		if ((flags & PKG_OPEN_TRY) == 0)
			pkg_emit_error("%s is not a valid package: no manifest found", path);
	}

	cleanup:
	if (retcode != EPKG_OK && retcode != EPKG_END) {
		if (*a != NULL) {
			archive_read_close(*a);
			archive_read_free(*a);
		}
		free(pkg);
		*pkg_p = NULL;
		*a = NULL;
		*ae = NULL;
	}

	return (retcode);
}

int
pkg_validate(struct pkg *pkg, struct pkgdb *db)
{
	assert(pkg != NULL);
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
					PKG_LOAD_REQUIRES|PKG_LOAD_PROVIDES|
					PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
					PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	if (pkg->uid == NULL) {
		/* Keep that part for the day we have to change it */
		/* Generate uid from name*/
		if (pkg->name == NULL)
			return (EPKG_FATAL);

		pkg->uid = xstrdup(pkg->name);
	}

	if (pkg->digest == NULL || !pkg_checksum_is_valid(pkg->digest,
			strlen(pkg->digest))) {
		/* Calculate new digest */
		if (pkgdb_ensure_loaded(db, pkg, flags)) {
			return (pkg_checksum_calculate(pkg, db));
		}
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkg_test_filesum(struct pkg *pkg)
{
	struct pkg_file *f = NULL;
	int rc = EPKG_OK;
	int ret;

	assert(pkg != NULL);

	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (f->sum != NULL) {
			ret = pkg_checksum_validate_file(f->path, f->sum);
			if (ret != 0) {
				if (ret == ENOENT)
					pkg_emit_file_missing(pkg, f);
				else
					pkg_emit_file_mismatch(pkg, f, f->sum);
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
	hardlinks_t *hl = NULL;
	int64_t flatsize = 0;
	struct stat st;
	bool regular = false;
	char *sum;
	int rc = EPKG_OK;

	hl = kh_init_hardlinks();
	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (lstat(f->path, &st) != 0)
			continue;
		regular = true;
		sum = pkg_checksum_generate_file(f->path,
		    PKG_HASH_TYPE_SHA256_HEX);

		if (S_ISLNK(st.st_mode))
			regular = false;

		if (sum == NULL) {
			rc = EPKG_FATAL;
			break;
		}

		if (st.st_nlink > 1)
			regular = !check_for_hardlink(hl, &st);

		if (regular)
			flatsize += st.st_size;
		
		if (strcmp(sum, f->sum) != 0)
			pkgdb_file_set_cksum(db, f, sum);
		free(sum);
	}
	kh_destroy_hardlinks(hl);

	if (flatsize != pkg->flatsize)
		pkg->flatsize = flatsize;

	return (rc);
}

int
pkg_try_installed(struct pkgdb *db, const char *name,
		struct pkg **pkg, unsigned flags) {
	struct pkgdb_it *it = NULL;
	int ret = EPKG_FATAL;

	if ((it = pkgdb_query(db, name, MATCH_EXACT)) == NULL)
		return (EPKG_FATAL);

	ret = pkgdb_it_next(it, pkg, flags);
	pkgdb_it_free(it);

	return (ret);
}

int
pkg_is_installed(struct pkgdb *db, const char *name)
{
	struct pkg *pkg = NULL;
	int ret = EPKG_FATAL;

	ret = pkg_try_installed(db, name, &pkg, PKG_LOAD_BASIC);
	pkg_free(pkg);

	return (ret);
}

bool
pkg_need_message(struct pkg *p, struct pkg *old)
{
	bool ret = true;

	if (old != NULL) {
		if (p->message->maximum_version) {
			ret = (pkg_version_cmp(old->version, p->message->maximum_version)
					<= 0);
		}
		if (ret && p->message->minimum_version) {
			ret = (pkg_version_cmp(old->version, p->message->maximum_version)
								>= 0);
		}
	}

	return (ret);
}

bool
pkg_has_message(struct pkg *p)
{
	return (p->message != NULL);
}

bool
pkg_is_locked(const struct pkg * restrict p)
{
	assert(p != NULL);

	return (p->locked);
}

bool
pkg_is_config_file(struct pkg *p, const char *path,
    const struct pkg_file **file,
    struct pkg_config_file **cfile)
{

	*file = NULL;
	*cfile = NULL;

	if (kh_count(p->config_files) == 0)
		return (false);

	kh_find(pkg_files, p->filehash, path, *file);
	if (*file == NULL)
		return (false);

	kh_find(pkg_config_files, p->config_files, path, *cfile);
	if (cfile == NULL) {
		*file = NULL;
		return (false);
	}

	return (true);
}

struct pkg_dir *
pkg_get_dir(struct pkg *p, const char *path)
{
	struct pkg_dir *d;

	kh_find(pkg_dirs, p->dirhash, path, d);

	return (d);
}

struct pkg_file *
pkg_get_file(struct pkg *p, const char *path)
{
	struct pkg_file *f;

	kh_find(pkg_files, p->filehash, path, f);

	return (f);
}

bool
pkg_has_file(struct pkg *p, const char *path)
{
	return (kh_contains(pkg_files, p->filehash, path));
}

bool
pkg_has_dir(struct pkg *p, const char *path)
{
	return (kh_contains(pkg_dirs, p->dirhash, path));
}

int
pkg_open_root_fd(struct pkg *pkg)
{
	const char *path;

	if (pkg->rootfd != -1)
		return (EPKG_OK);

	path = pkg_kv_get(&pkg->annotations, "relocated");
	if (path == NULL) {
#ifdef F_DUPFD_CLOEXEC
		if ((pkg->rootfd = fcntl(rootfd, F_DUPFD_CLOEXEC, 0)) == -1) {
#else
		if ((pkg->rootfd = dup(rootfd)) == -1 || fcntl(pkg->rootfd, F_SETFD, FD_CLOEXEC) == -1) {
#endif
			pkg_emit_errno("dup2", "rootfd");
			return (EPKG_FATAL);
		}
		return (EPKG_OK);
	}

	pkg_absolutepath(path, pkg->rootpath, sizeof(pkg->rootpath), false);

	if ((pkg->rootfd = openat(rootfd, pkg->rootpath + 1, O_DIRECTORY|O_CLOEXEC)) >= 0 )
		return (EPKG_OK);

	pkg->rootpath[0] = '\0';
	pkg_emit_errno("open", path);

	return (EPKG_FATAL);
}

int
pkg_message_from_ucl(struct pkg *pkg, const ucl_object_t *obj)
{
	struct pkg_message *msg = NULL;
	const ucl_object_t *elt, *cur;
	ucl_object_iter_t it = NULL;

	if (ucl_object_type(obj) == UCL_STRING) {
		msg = xcalloc(1, sizeof(*msg));
		msg->str = xstrdup(ucl_object_tostring(obj));
		msg->type = PKG_MESSAGE_ALWAYS;
		DL_APPEND(pkg->message, msg);
		return (EPKG_OK);
	}

	/* New format of pkg message */
	if (ucl_object_type(obj) != UCL_ARRAY)
		pkg_emit_error("package message badly formatted, an array was"
		    " expected");

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		elt = ucl_object_find_key(cur, "message");

		if (elt == NULL || ucl_object_type(elt) != UCL_STRING) {
			pkg_emit_error("package message lacks 'message' key"
			    " that is required");

			return (EPKG_FATAL);
		}

		msg = xcalloc(1, sizeof(*msg));

		msg->str = xstrdup(ucl_object_tostring(elt));
		msg->type = PKG_MESSAGE_ALWAYS;
		elt = ucl_object_find_key(cur, "type");
		if (elt != NULL && ucl_object_type(elt) == UCL_STRING) {
			if (strcasecmp(ucl_object_tostring(elt), "install") == 0)
				msg->type = PKG_MESSAGE_INSTALL;
			else if (strcasecmp(ucl_object_tostring(elt), "remove") == 0)
				msg->type = PKG_MESSAGE_REMOVE;
			else if (strcasecmp(ucl_object_tostring(elt), "upgrade") == 0)
				msg->type = PKG_MESSAGE_UPGRADE;
			else
				pkg_emit_error("Unknown message type,"
				    " message will always be printed");
		}
		if (msg->type != PKG_MESSAGE_UPGRADE) {
			DL_APPEND(pkg->message, msg);
			continue;
		}

		elt = ucl_object_find_key(cur, "minimum_version");
		if (elt != NULL && ucl_object_type(elt) == UCL_STRING) {
			msg->minimum_version = xstrdup(ucl_object_tostring(elt));
		}

		elt = ucl_object_find_key(cur, "maximum_version");
		if (elt != NULL && ucl_object_type(elt) == UCL_STRING) {
			msg->maximum_version = xstrdup(ucl_object_tostring(elt));
		}

		DL_APPEND(pkg->message, msg);
	}

	return (EPKG_OK);
}

int
pkg_message_from_str(struct pkg *pkg, const char *str, size_t len)
{
	struct ucl_parser *parser;
	ucl_object_t *obj;
	int ret = EPKG_FATAL;

	assert(str != NULL);

	if (len == 0) {
		len = strlen(str);
	}

	parser = ucl_parser_new(UCL_PARSER_NO_FILEVARS);

	if (ucl_parser_add_chunk(parser, (const unsigned char*)str, len)) {
		obj = ucl_parser_get_object(parser);
		ucl_parser_free(parser);

		ret = pkg_message_from_ucl(pkg, obj);
		ucl_object_unref(obj);

		return (ret);
	}

	ucl_parser_free (parser);

	return (ret);
}

ucl_object_t*
pkg_message_to_ucl(const struct pkg *pkg)
{
	struct pkg_message *msg;
	ucl_object_t *array;
	ucl_object_t *obj;

	array = ucl_object_typed_new(UCL_ARRAY);
	LL_FOREACH(pkg->message, msg) {
		obj = ucl_object_typed_new (UCL_OBJECT);

		ucl_object_insert_key(obj,
		    ucl_object_fromstring_common(msg->str, 0,
		    UCL_STRING_RAW|UCL_STRING_TRIM),
		    "message", 0, false);

		switch (msg->type) {
		case PKG_MESSAGE_ALWAYS:
			break;
		case PKG_MESSAGE_INSTALL:
			ucl_object_insert_key(obj,
			    ucl_object_fromstring("install"),
			    "type", 0, false);
			break;
		case PKG_MESSAGE_UPGRADE:
			ucl_object_insert_key(obj,
			    ucl_object_fromstring("upgrade"),
			    "type", 0, false);
			break;
		case PKG_MESSAGE_REMOVE:
			ucl_object_insert_key(obj,
			    ucl_object_fromstring("remove"),
			    "type", 0, false);
			break;
		}
		if (msg->maximum_version) {
			ucl_object_insert_key(obj,
			    ucl_object_fromstring(msg->maximum_version),
			    "maximum_version", 0, false);
		}
		if (msg->minimum_version) {
			ucl_object_insert_key(obj,
			    ucl_object_fromstring(msg->minimum_version),
			    "minimum_version", 0, false);
		}
		ucl_array_append(array, obj);
	}

	return (array);
}

char*
pkg_message_to_str(struct pkg *pkg)
{
	ucl_object_t *obj;
	char *ret = NULL;

	if (pkg->message == NULL) {
		return (NULL);
	}

	obj = pkg_message_to_ucl(pkg);
	ret = ucl_object_emit(obj, UCL_EMIT_JSON_COMPACT);
	ucl_object_unref(obj);

	return (ret);
}
