/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include "private/utils.h"

static ucl_object_t *manifest_schema = NULL;

struct pkg_key pkg_keys[PKG_NUM_FIELDS] = {
	[PKG_ORIGIN] = { "origin", UCL_STRING },
	[PKG_NAME] = { "name", UCL_STRING },
	[PKG_VERSION] = { "version", UCL_STRING },
	[PKG_COMMENT] = { "comment", UCL_STRING },
	[PKG_DESC] = { "desc", UCL_STRING },
	[PKG_MTREE] = { "mtree", UCL_STRING },
	[PKG_MESSAGE] = { "message", UCL_STRING },
	[PKG_ABI] = { "abi", UCL_STRING },
	[PKG_ARCH] = { "arch", UCL_STRING },
	[PKG_MAINTAINER] = { "maintainer", UCL_STRING },
	[PKG_WWW] = { "www", UCL_STRING },
	[PKG_PREFIX] = { "prefix", UCL_STRING },
	[PKG_REPOPATH] = { "repopath", UCL_STRING },
	[PKG_CKSUM] = { "sum", UCL_STRING },
	[PKG_OLD_VERSION] = { "oldversion", UCL_STRING },
	[PKG_REPONAME] = { "reponame", UCL_STRING },
	[PKG_REPOURL] = { "repourl", UCL_STRING },
	[PKG_DIGEST] = { "digest", UCL_STRING },
	[PKG_REASON] = { "reason", UCL_STRING },
	[PKG_FLATSIZE] = { "flatsize", UCL_INT },
	[PKG_OLD_FLATSIZE] = { "oldflatsize", UCL_INT },
	[PKG_PKGSIZE] = { "pkgsize", UCL_INT },
	[PKG_LICENSE_LOGIC] = { "licenselogic", UCL_INT },
	[PKG_AUTOMATIC] = { "automatic", UCL_BOOLEAN },
	[PKG_LOCKED] = { "locked", UCL_BOOLEAN },
	[PKG_ROWID] = { "rowid", UCL_INT },
	[PKG_TIME] = { "time", UCL_INT },
	[PKG_ANNOTATIONS] = { "annotations", UCL_OBJECT },
	[PKG_LICENSES] = { "licenses", UCL_ARRAY },
	[PKG_CATEGORIES] = { "categories", UCL_ARRAY },
	[PKG_UNIQUEID] = { "uniqueid", UCL_STRING },
	[PKG_OLD_DIGEST] = { "olddigest", UCL_STRING },
};

int
pkg_new(struct pkg **pkg, pkg_t type)
{
	if ((*pkg = calloc(1, sizeof(struct pkg))) == NULL) {
		pkg_emit_errno("calloc", "pkg");
		return EPKG_FATAL;
	}

	(*pkg)->type = type;
	(*pkg)->rootfd = -1;

	return (EPKG_OK);
}

void
pkg_reset(struct pkg *pkg, pkg_t type)
{
	int i;

	if (pkg == NULL)
		return;

	pkg->flags &= ~PKG_LOAD_CATEGORIES;
	pkg->flags &= ~PKG_LOAD_LICENSES;
	pkg->flags &= ~PKG_LOAD_ANNOTATIONS;

	for (i = 0; i < PKG_NUM_SCRIPTS; i++)
		sbuf_reset(pkg->scripts[i]);
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
	if (pkg->rootfd != -1)
		close(pkg->rootfd);
	pkg->rootfd = -1;
	pkg->rootpath[0] = '\0';

	pkg->type = type;
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
	free(pkg->message);
	free(pkg->prefix);
	free(pkg->comment);
	free(pkg->desc);
	free(pkg->sum);
	free(pkg->repopath);
	free(pkg->repourl);
	free(pkg->reason);

	for (int i = 0; i < PKG_NUM_SCRIPTS; i++)
		sbuf_free(pkg->scripts[i]);

	pkg_list_free(pkg, PKG_DEPS);
	pkg_list_free(pkg, PKG_RDEPS);
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg_list_free(pkg, PKG_OPTIONS);
	pkg_list_free(pkg, PKG_USERS);
	pkg_list_free(pkg, PKG_GROUPS);
	pkg_list_free(pkg, PKG_SHLIBS_REQUIRED);
	pkg_list_free(pkg, PKG_SHLIBS_PROVIDED);

	LL_FREE(pkg->categories, pkg_strel_free);
	LL_FREE(pkg->licenses, pkg_strel_free);
	LL_FREE(pkg->annotations, pkg_kv_free);

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

static ucl_object_t *
manifest_schema_open(pkg_t type __unused)
{
	struct ucl_parser *parser;
	static const char manifest_schema_str[] = ""
		"{"
		"  type = object;"
		"  properties {"
		"    origin = { type = string };"
		"    name = { type = string };"
		"    comment = { type = string };"
		"    desc = { type = string };"
		"    mtree = { type = string };"
		"    message = { type = string };"
		"    maintainer = { type = string };"
		"    arch = { type = string };"
		"    abi = { type = string };"
		"    www = { type = string };"
		"    prefix = { type = string };"
		"    digest = { type = string };"
		"    repopath = { type = string };"
		"    sum = { type = string };"
		"    oldversion = { type = string };"
		"    reponame = { type = string };"
		"    repourl = { type = string };"
		"    reason = { type = string };"
		"    flatsize = { type = integer }; "
		"    oldflatsize = { type = integer }; "
		"    pkgsize = { type = integer }; "
		"    locked = { type = boolean }; "
		"    rowid = { type = integer }; "
		"    time = { type = integer }; "
		"    annotations = { type = object }; "
		"    licenses = { "
		"      type = array; "
		"      items = { type = string }; "
		"      uniqueItems = true ;"
		"    };"
		"    categories = { "
		"      type = array; "
		"      items = { type = string }; "
		"      uniqueItems = true ;"
		"    };"
		"  }\n"
		"  required = ["
		"    origin,"
		"    name,"
		"    comment,"
		"    version,"
		"    desc,"
		"    maintainer,"
		"    www,"
		"    prefix,"
		"  ]"
		"}";

	if (manifest_schema != NULL)
		return (manifest_schema);

	parser = ucl_parser_new(0);
	if (!ucl_parser_add_chunk(parser, manifest_schema_str,
	    sizeof(manifest_schema_str) -1)) {
		pkg_emit_error("Cannot parse manifest schema: %s",
		    ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return (NULL);
	}

	manifest_schema = ucl_parser_get_object(parser);
	ucl_parser_free(parser);

	return (manifest_schema);
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
			*va_arg(ap, const char **) = pkg->message;
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
		case PKG_CATEGORIES:
			*va_arg(ap, const struct pkg_strel **) = pkg->categories;
			break;
		case PKG_LICENSES:
			*va_arg(ap, const struct pkg_strel **) = pkg->licenses;
			break;
		case PKG_UNIQUEID:
			*va_arg(ap, const char **) = pkg->uid;
			break;
		case PKG_OLD_DIGEST:
			*va_arg(ap, const char **) = pkg->old_digest;
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

	while ((attr = va_arg(ap, int)) > 0) {
		if (attr >= PKG_NUM_FIELDS || attr <= 0) {
			pkg_emit_error("Bad argument on pkg_set %s", attr);
			return (EPKG_FATAL);
		}

		switch (attr) {
		case PKG_NAME:
			free(pkg->name);
			pkg->name = strdup(va_arg(ap, const char *));
			free(pkg->uid);
			pkg->uid = strdup(pkg->name);
			break;
		case PKG_ORIGIN:
			free(pkg->origin);
			pkg->origin = strdup(va_arg(ap, const char *));
			break;
		case PKG_VERSION:
			free(pkg->version);
			pkg->version = strdup(va_arg(ap, const char *));
			break;
		case PKG_COMMENT:
			free(pkg->comment);
			pkg->comment = strdup(va_arg(ap, const char *));
			break;
		case PKG_DESC:
			free(pkg->desc);
			pkg->desc = strdup(va_arg(ap, const char *));
			break;
		case PKG_MTREE:
			(void)va_arg(ap, const char *);
			break;
		case PKG_MESSAGE:
			free(pkg->message);
			pkg->message = strdup(va_arg(ap, const char *));
			break;
		case PKG_ARCH:
			free(pkg->arch);
			pkg->arch = strdup(va_arg(ap, const char *));
			break;
		case PKG_ABI:
			free(pkg->abi);
			pkg->abi = strdup(va_arg(ap, const char *));
			break;
		case PKG_MAINTAINER:
			free(pkg->maintainer);
			pkg->maintainer = strdup(va_arg(ap, const char *));
			break;
		case PKG_WWW:
			free(pkg->www);
			pkg->www = strdup(va_arg(ap, const char *));
			break;
		case PKG_PREFIX:
			free(pkg->prefix);
			pkg->prefix = strdup(va_arg(ap, const char *));
			break;
		case PKG_REPOPATH:
			free(pkg->repopath);
			pkg->repopath = strdup(va_arg(ap, const char *));
			break;
		case PKG_CKSUM:
			free(pkg->sum);
			pkg->sum = strdup(va_arg(ap, const char *));
			break;
		case PKG_OLD_VERSION:
			free(pkg->old_version);
			pkg->old_version = strdup(va_arg(ap, const char *));
			break;
		case PKG_REPONAME:
			free(pkg->reponame);
			pkg->reponame = strdup(va_arg(ap, const char *));
			break;
		case PKG_REPOURL:
			free(pkg->repourl);
			pkg->repourl = strdup(va_arg(ap, const char *));
			break;
		case PKG_DIGEST:
			free(pkg->digest);
			pkg->digest = strdup(va_arg(ap, const char *));
			break;
		case PKG_REASON:
			free(pkg->digest);
			pkg->digest = strdup(va_arg(ap, const char *));
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
			pkg->pkgsize = (bool)va_arg(ap, int);
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
	off_t size = 0;
	int ret = EPKG_OK;

	assert(pkg != NULL);
	assert(path != NULL);

	if ((ret = file_to_bufferat(fd, path, &buf, &size)) !=  EPKG_OK)
		return (ret);

	while (trimcr && buf[strlen(buf) - 1] == '\n')
		buf[strlen(buf) - 1] = '\0';

	ret = pkg_set(pkg, attr, buf);

	free(buf);

	return (ret);
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
pkg_config_files(const struct pkg *pkg, struct pkg_config_file **f)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->config_files, (*f));
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
pkg_conflicts(const struct pkg *pkg, struct pkg_conflict **c)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->conflicts, (*c));
}

int
pkg_provides(const struct pkg *pkg, struct pkg_provide **c)
{
	assert(pkg != NULL);

	HASH_NEXT(pkg->provides, (*c));
}

int
pkg_adduid(struct pkg *pkg, const char *name, const char *uidstr)
{
	struct pkg_user *u = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->users, name, u);
	if (u != NULL) {
		if (developer_mode) {
			pkg_emit_error("duplicate user listing: %s, fatal (developer mode)", name);
			return (EPKG_FATAL);
		} else {
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

	HASH_FIND_STR(pkg->groups, name, g);
	if (g != NULL) {
		if (developer_mode) {
			pkg_emit_error("duplicate group listing: %s, fatal (developer mode)", name);
			return (EPKG_FATAL);
		} else {
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

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	assert(origin != NULL && origin[0] != '\0');
	assert(version != NULL && version[0] != '\0');

	pkg_debug(3, "Pkg: add a new dependency origin: %s, name: %s, version: %s", origin, name, version);
	HASH_FIND_STR(pkg->deps, name, d);
	if (d != NULL) {
		if (developer_mode) {
			pkg_emit_error("%s-%s: duplicate dependency listing: %s-%s, fatal (developer mode)",
			    pkg->name, pkg->version, name, version);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("%s-%s: duplicate dependency listing: %s-%s, ignoring",
			    pkg->name, pkg->version, name, version);
			return (EPKG_OK);
		}
	}

	pkg_dep_new(&d);

	d->origin = strdup(origin);
	d->name = strdup(name);
	d->version = strdup(version);
	d->uid = strdup(name);
	d->locked = locked;

	HASH_ADD_KEYPTR(hh, pkg->deps, d->name, strlen(d->name), d);

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

	pkg_debug(3, "Pkg: add a new reverse dependency origin: %s, name: %s, version: %s", origin, name, version);
	pkg_dep_new(&d);

	d->origin = strdup(origin);
	d->name = strdup(name);
	d->version = strdup(version);
	d->uid = strdup(name);
	d->locked = locked;

	HASH_ADD_KEYPTR(hh, pkg->rdeps, d->origin, strlen(d->origin), d);

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
	char abspath[MAXPATHLEN];

	assert(pkg != NULL);
	assert(path != NULL && path[0] != '\0');

	path = pkg_absolutepath(path, abspath, sizeof(abspath));
	pkg_debug(3, "Pkg: add new file '%s'", path);

	if (check_duplicates) {
		HASH_FIND_STR(pkg->files, path, f);
		if (f != NULL) {
			if (developer_mode) {
				pkg_emit_error("duplicate file listing: %s, fatal (developer mode)", f->path);
				return (EPKG_FATAL);
			} else {
				pkg_emit_error("duplicate file listing: %s, ignoring", f->path);
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

	HASH_ADD_STR(pkg->files, path, f);

	return (EPKG_OK);
}

int
pkg_addconfig_file(struct pkg *pkg, const char *path, const char *content)
{
	struct pkg_config_file *f;
	char abspath[MAXPATHLEN];

	path = pkg_absolutepath(path, abspath, sizeof(abspath));
	pkg_debug(3, "Pkg: add new config file '%s'", path);

	HASH_FIND_STR(pkg->config_files, path, f);
	if (f != NULL) {
		if (developer_mode) {
			pkg_emit_error("duplicate file listing: %s, fatal (developer mode)", f->path);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate file listing: %s, ignoring", f->path);
		}
	}
	pkg_config_file_new(&f);
	strlcpy(f->path, path, sizeof(f->path));

	if (content != NULL)
		f->content = strdup(content);

	HASH_ADD_STR(pkg->config_files, path, f);

	return (EPKG_OK);
}

int
pkg_strel_add(struct pkg_strel **list, const char *val, const char *title)
{
	struct pkg_strel *c;

	assert(val != NULL);
	assert(title != NULL);

	LL_FOREACH(*list, c) {
		if (strcmp(c->value, val) == 0) {
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
	}

	pkg_strel_new(&c, val);
	LL_APPEND(*list, c);

	return (EPKG_OK);
}

int
pkg_adddir(struct pkg *pkg, const char *path, bool try, bool check_duplicates)
{
	return(pkg_adddir_attr(pkg, path, NULL, NULL, 0, try, check_duplicates));
}

int
pkg_adddir_attr(struct pkg *pkg, const char *path, const char *uname, const char *gname, mode_t perm, bool try __unused, bool check_duplicates)
{
	struct pkg_dir *d = NULL;
	char abspath[MAXPATHLEN];

	assert(pkg != NULL);
	assert(path != NULL && path[0] != '\0');

	path = pkg_absolutepath(path, abspath, sizeof(abspath));
	pkg_debug(3, "Pkg: add new directory '%s'", path);
	if (check_duplicates) {
		HASH_FIND_STR(pkg->dirs, path, d);
		if (d != NULL) {
			if (developer_mode) {
				pkg_emit_error("duplicate directory listing: %s, fatal (developer mode)", d->path);
				return (EPKG_FATAL);
			} else {
				pkg_emit_error("duplicate directory listing: %s, ignoring", d->path);
				return (EPKG_OK);
			}
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
	HASH_FIND_STR(pkg->options, key, o);
	if (o == NULL) {
		pkg_option_new(&o);
		o->key = strdup(key);
	} else if ( o->value != NULL) {
		if (developer_mode) {
			pkg_emit_error("duplicate options listing: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate options listing: %s, ignoring", key);
			return (EPKG_OK);
		}
	}

	o->value = strdup(value);
	HASH_ADD_KEYPTR(hh, pkg->options, o->key, strlen(o->key), o);

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

	HASH_FIND_STR(pkg->options, key, o);
	if (o == NULL) {
		pkg_option_new(&o);
		o->key = strdup(key);
	} else if ( o->default_value != NULL) {
		if (developer_mode) {
			pkg_emit_error("duplicate default value for option: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate default value for option: %s, ignoring", key);
			return (EPKG_OK);
		}
	}

	o->default_value = strdup(default_value);
	HASH_ADD_KEYPTR(hh, pkg->options, o->default_value,
	    strlen(o->default_value), o);

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

	HASH_FIND_STR(pkg->options, key, o);
	if (o == NULL) {
		pkg_option_new(&o);
		o->key = strdup(key);
	} else if ( o->description != NULL) {
		if (developer_mode) {
			pkg_emit_error("duplicate description for option: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate description for option: %s, ignoring", key);
			return (EPKG_OK);
		}
	}

	o->description = strdup(description);
	HASH_ADD_KEYPTR(hh, pkg->options, o->description,
	    strlen(o->description), o);

	return (EPKG_OK);
}

int
pkg_addshlib_required(struct pkg *pkg, const char *name)
{
	struct pkg_shlib *s = NULL, *f;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	pkg_shlib_new(&s);
	s->name = strdup(name);

	HASH_FIND_STR(pkg->shlibs_required, s->name, f);
	/* silently ignore duplicates in case of shlibs */
	if (f != NULL) {
		pkg_shlib_free(s);
		return (EPKG_OK);
	}

	HASH_ADD_KEYPTR(hh, pkg->shlibs_required, s->name, strlen(s->name), s);

	pkg_debug(3, "added shlib deps for %s on %s", pkg->name, name);

	return (EPKG_OK);
}

int
pkg_addshlib_provided(struct pkg *pkg, const char *name)
{
	struct pkg_shlib *s = NULL, *f;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	/* ignore files which are not starting with lib */
	if (strncmp(name, "lib", 3) != 0)
		return (EPKG_OK);

	pkg_shlib_new(&s);
	s->name = strdup(name);
	HASH_FIND_STR(pkg->shlibs_provided, s->name, f);
	/* silently ignore duplicates in case of shlibs */
	if (f != NULL) {
		pkg_shlib_free(s);
		return (EPKG_OK);
	}

	HASH_ADD_KEYPTR(hh, pkg->shlibs_provided, s->name, strlen(s->name), s);

	pkg_debug(3, "added shlib provide %s for %s", pkg->name, pkg->origin);

	return (EPKG_OK);
}

int
pkg_addconflict(struct pkg *pkg, const char *uniqueid)
{
	struct pkg_conflict *c = NULL;

	assert(pkg != NULL);
	assert(uniqueid != NULL && uniqueid[0] != '\0');

	HASH_FIND_STR(pkg->conflicts, __DECONST(char *, uniqueid), c);
	/* silently ignore duplicates in case of conflicts */
	if (c != NULL)
		return (EPKG_OK);

	pkg_conflict_new(&c);
	c->uid = strdup(uniqueid);
	pkg_debug(3, "Pkg: add a new conflict origin: %s, with %s", pkg->uid, uniqueid);

	HASH_ADD_KEYPTR(hh, pkg->conflicts, c->uid, strlen(c->uid), c);

	return (EPKG_OK);
}

int
pkg_addprovide(struct pkg *pkg, const char *name)
{
	struct pkg_provide *p = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->provides, __DECONST(char *, name), p);
	/* silently ignore duplicates in case of conflicts */
	if (p != NULL)
		return (EPKG_OK);

	pkg_provide_new(&p);
	p->provide = strdup(name);

	HASH_ADD_KEYPTR(hh, pkg->provides, p->provide, strlen(p->provide), p);

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

	pkg_kv_new(&kv, key, val);
	LL_APPEND(*list, kv);

	return (EPKG_OK);
}

int
pkg_list_count(const struct pkg *pkg, pkg_list list)
{
	switch (list) {
	case PKG_DEPS:
		return (HASH_COUNT(pkg->deps));
	case PKG_RDEPS:
		return (HASH_COUNT(pkg->rdeps));
	case PKG_OPTIONS:
		return (HASH_COUNT(pkg->options));
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
	case PKG_CONFLICTS:
		return (HASH_COUNT(pkg->conflicts));
	case PKG_PROVIDES:
		return (HASH_COUNT(pkg->provides));
	case PKG_CONFIG_FILES:
		return (HASH_COUNT(pkg->config_files));
	}
	
	return (0);
}

void
pkg_list_free(struct pkg *pkg, pkg_list list)  {
	switch (list) {
	case PKG_DEPS:
		HASH_FREE(pkg->deps, pkg_dep_free);
		pkg->flags &= ~PKG_LOAD_DEPS;
		break;
	case PKG_RDEPS:
		HASH_FREE(pkg->rdeps, pkg_dep_free);
		pkg->flags &= ~PKG_LOAD_RDEPS;
		break;
	case PKG_OPTIONS:
		HASH_FREE(pkg->options, pkg_option_free);
		pkg->flags &= ~PKG_LOAD_OPTIONS;
		break;
	case PKG_FILES:
	case PKG_CONFIG_FILES:
		HASH_FREE(pkg->files, pkg_file_free);
		HASH_FREE(pkg->config_files, pkg_config_file_free);
		pkg->flags &= ~PKG_LOAD_FILES;
		break;
	case PKG_DIRS:
		HASH_FREE(pkg->dirs, pkg_dir_free);
		pkg->flags &= ~PKG_LOAD_DIRS;
		break;
	case PKG_USERS:
		HASH_FREE(pkg->users, pkg_user_free);
		pkg->flags &= ~PKG_LOAD_USERS;
		break;
	case PKG_GROUPS:
		HASH_FREE(pkg->groups, pkg_group_free);
		pkg->flags &= ~PKG_LOAD_GROUPS;
		break;
	case PKG_SHLIBS_REQUIRED:
		HASH_FREE(pkg->shlibs_required, pkg_shlib_free);
		pkg->flags &= ~PKG_LOAD_SHLIBS_REQUIRED;
		break;
	case PKG_SHLIBS_PROVIDED:
		HASH_FREE(pkg->shlibs_provided, pkg_shlib_free);
		pkg->flags &= ~PKG_LOAD_SHLIBS_PROVIDED;
		break;
	case PKG_CONFLICTS:
		HASH_FREE(pkg->conflicts, pkg_conflict_free);
		pkg->flags &= ~PKG_LOAD_CONFLICTS;
		break;
	case PKG_PROVIDES:
		HASH_FREE(pkg->provides, pkg_provide_free);
		pkg->flags &= ~PKG_LOAD_PROVIDES;
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
	struct pkg	*pkg;
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
			buffer = malloc(len);
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
			buffer = malloc(len);
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
		*a = NULL;
		*ae = NULL;
	}

	return (retcode);
}

int
pkg_validate(struct pkg *pkg)
{
	assert(pkg != NULL);

	if (pkg->uid == NULL) {
		/* Keep that part for the day we have to change it */
		/* Generate uid from name*/
		if (pkg->name == NULL)
			return (EPKG_FATAL);

		pkg->uid = strdup(pkg->name);
	}

	if (pkg->digest == NULL || !pkg_checksum_is_valid(pkg->digest, strlen(pkg->digest))) {
		/* Calculate new digest */
		return (pkg_checksum_calculate(pkg, NULL));
	}

	return (EPKG_OK);
}

int
pkg_copy_tree(struct pkg *pkg, const char *src, const char *dest)
{
	struct packing *pack;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	char spath[MAXPATHLEN];
	char dpath[MAXPATHLEN];

	if (packing_init(&pack, dest, 0, true) != EPKG_OK) {
		/* TODO */
		return EPKG_FATAL;
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		snprintf(spath, sizeof(spath), "%s%s", src, dir->path);
		snprintf(dpath, sizeof(dpath), "%s%s", dest, dir->path);
		packing_append_file_attr(pack, spath, dpath,
		    dir->uname, dir->gname, dir->perm);
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		snprintf(spath, sizeof(spath), "%s%s", src, file->path);
		snprintf(dpath, sizeof(dpath), "%s%s", dest, file->path);
		packing_append_file_attr(pack, spath, dpath,
		    file->uname, file->gname, file->perm);
	}

	return (packing_finish(pack));
}

int
pkg_test_filesum(struct pkg *pkg)
{
	struct pkg_file *f = NULL;
	struct stat	 st;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	int rc = EPKG_OK;

	assert(pkg != NULL);

	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (f->sum[0] != '\0') {
			if (lstat(f->path, &st) == -1) {
				pkg_emit_errno("pkg_create_from_dir", "lstat failed");
				return (EPKG_FATAL);
			}
			if (S_ISLNK(st.st_mode)) {
				if (pkg_symlink_cksum(f->path, NULL, sha256) != EPKG_OK)
					return (EPKG_FATAL);
			}
			else {
				if (sha256_file(f->path, sha256) != EPKG_OK)
					return (EPKG_FATAL);

			}
			if (strcmp(sha256, f->sum) != 0) {
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
	struct hardlinks *hl = NULL;
	int64_t flatsize = 0;
	struct stat st;
	bool regular = false;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	int rc = EPKG_OK;

	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (lstat(f->path, &st) == 0) {
			regular = true;
			if (S_ISLNK(st.st_mode)) {
				regular = false;
				if (pkg_symlink_cksum(f->path, NULL, sha256)
				    != EPKG_OK) {
					rc = EPKG_FATAL;
					break;
				}
			} else {
				if (sha256_file(f->path, sha256) != EPKG_OK) {
					rc = EPKG_FATAL;
					break;
				}
			}

			if (st.st_nlink > 1)
				regular = !check_for_hardlink(&hl, &st);

			if (regular)
				flatsize += st.st_size;
		}
		if (strcmp(sha256, f->sum) != 0)
			pkgdb_file_set_cksum(db, f, sha256);
	}
	HASH_FREE(hl, free);

	if (flatsize != pkg->flatsize)
		pkg->flatsize = flatsize;

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
pkg_is_installed(struct pkgdb *db, const char *name)
{
	struct pkg *pkg = NULL;
	int ret = EPKG_FATAL;

	ret = pkg_try_installed(db, name, &pkg, PKG_LOAD_BASIC);
	pkg_free(pkg);

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
	struct pkg_file *f;
	struct pkg_config_file *cf;

	*file = NULL;
	*cfile = NULL;

	HASH_FIND_STR(p->files, path, f);
	if (f == NULL)
		return (false);

	HASH_FIND_STR(p->config_files, path, cf);
	if (cf == NULL)
		return (false);

	*file = f;
	*cfile = cf;

	return (true);
}

bool
pkg_has_file(struct pkg *p, const char *path)
{
	struct pkg_file *f;

	HASH_FIND_STR(p->files, path, f);

	return (f != NULL ? true : false);
}

bool
pkg_has_dir(struct pkg *p, const char *path)
{
	struct pkg_dir *d;

	HASH_FIND_STR(p->dirs, path, d);

	return (d != NULL ? true : false);
}

int
pkg_open_root_fd(struct pkg *pkg)
{
	const char *path;
	const ucl_object_t 	*obj;

	obj = NULL;
	if (pkg->rootfd != -1)
		return (EPKG_OK);

	path = pkg_kv_get(&pkg->annotations, "relocated");
	if (path == NULL)
		path = "/";

	strlcpy(pkg->rootpath, path, sizeof(pkg->rootpath));

	if ((pkg->rootfd = open(path , O_DIRECTORY|O_CLOEXEC)) >= 0 )
		return (EPKG_OK);

	pkg_emit_errno("open", obj ? pkg_object_string(obj) : "/");

	return (EPKG_FATAL);
}
