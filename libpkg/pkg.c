/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <stdlib.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

static ucl_object_t *manifest_schema = NULL;

int
pkg_new(struct pkg **pkg, pkg_t type)
{
	if ((*pkg = calloc(1, sizeof(struct pkg))) == NULL) {
		pkg_emit_errno("calloc", "pkg");
		return EPKG_FATAL;
	}

	(*pkg)->fields = ucl_object_typed_new(UCL_OBJECT);
	(*pkg)->type = type;

	return (EPKG_OK);
}

void
pkg_reset(struct pkg *pkg, pkg_t type)
{
	int i;

	if (pkg == NULL)
		return;

	ucl_object_unref(pkg->fields);
	pkg->fields = ucl_object_typed_new(UCL_OBJECT);
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

	pkg->type = type;
}

void
pkg_free(struct pkg *pkg)
{
	if (pkg == NULL)
		return;

	ucl_object_unref(pkg->fields);

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
		"    desc,"
		"    maintainer,"
		"    arch,"
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
	ucl_object_t *schema;
	struct ucl_schema_error err;

	schema = manifest_schema_open(pkg->type);

	if (schema == NULL)
		return (EPKG_FATAL);

	if (!ucl_object_validate(schema, pkg->fields, &err)) {
		pkg_emit_error("Invalid package: %s", err.msg);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
pkg_vget(const struct pkg * restrict pkg, va_list ap)
{
	int attr;
	const ucl_object_t *obj;

	while ((attr = va_arg(ap, int)) > 0) {

		if (attr >= PKG_NUM_FIELDS || attr <= 0) {
			pkg_emit_error("Bad argument on pkg_get");
			return (EPKG_FATAL);
		}

		obj = ucl_object_find_key(pkg->fields, pkg_keys[attr].name);
		switch (pkg_keys[attr].type) {
		case UCL_STRING:
			if (obj == NULL) {
				*va_arg(ap, const char **) = NULL;
				break;
			}
			*va_arg(ap, const char **) = ucl_object_tostring_forced(obj);
			break;
		case UCL_BOOLEAN:
			if (obj == NULL) {
				*va_arg(ap, bool *) = false;
				break;
			}
			*va_arg(ap, bool *) = ucl_object_toboolean(obj);
			break;
		case UCL_INT:
			if (obj == NULL) {
				*va_arg(ap, int64_t *) = 0;
				break;
			}
			*va_arg(ap, int64_t *) = ucl_object_toint(obj);
			break;
		case UCL_OBJECT:
		case UCL_ARRAY:
			*va_arg(ap, const pkg_object **) = obj;
			break;
		default:
			va_arg(ap, void *); /* ignore */
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
	struct pkg_repo *r;
	char *buf = NULL;
	const char *data;
	const char *str;
	ucl_object_t *o;

	while ((attr = va_arg(ap, int)) > 0) {
		if (attr >= PKG_NUM_FIELDS || attr <= 0) {
			pkg_emit_error("Bad argument on pkg_get");
			return (EPKG_FATAL);
		}

		switch (pkg_keys[attr].type) {
		case UCL_STRING:
			str = va_arg(ap, const char *);
			data = str;

			if (attr == PKG_MTREE && !STARTS_WITH(str, "#mtree")) {
				asprintf(&buf, "#mtree\n%s", str);
				data = buf;
			}

			if (attr == PKG_REPOURL) {
				r = pkg_repo_find_ident(str);
				if (r == NULL)
					break;
				data = pkg_repo_url(r);
			}

			ucl_object_replace_key(pkg->fields,
			    ucl_object_fromstring_common(data, strlen(data), 0),
			    pkg_keys[attr].name, strlen(pkg_keys[attr].name), false);

			if (buf != NULL)
				free(buf);

			break;
		case UCL_BOOLEAN:
			ucl_object_replace_key(pkg->fields,
			    ucl_object_frombool((bool)va_arg(ap, int)),
			    pkg_keys[attr].name, strlen(pkg_keys[attr].name), false);
			break;
		case UCL_INT:
			ucl_object_replace_key(pkg->fields,
			    ucl_object_fromint(va_arg(ap, int64_t)),
			    pkg_keys[attr].name, strlen(pkg_keys[attr].name), false);
			break;
		case UCL_OBJECT:
		case UCL_ARRAY:
			o = va_arg(ap, ucl_object_t *);
			ucl_object_replace_key(pkg->fields, o,
			    pkg_keys[attr].name, strlen(pkg_keys[attr].name), false);
			break;
		default:
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

struct pkg_dep *
pkg_dep_lookup(const struct pkg *pkg, const char *origin)
{
	struct pkg_dep *d = NULL;

	assert(pkg != NULL);
	assert(origin != NULL);

	HASH_FIND_STR(pkg->deps, origin, d);

	return (d);
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
pkg_addlicense(struct pkg *pkg, const char *name)
{
	const pkg_object *o, *licenses;
	pkg_object *l, *lic;
	pkg_iter iter = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	pkg_get(pkg, PKG_LICENSES, &licenses);

	while ((o = pkg_object_iterate(licenses, &iter))) {
		if (strcmp(pkg_object_string(o), name) == 0) {
			if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
				pkg_emit_error("duplicate license listing: %s, fatal (developer mode)", name);
				return (EPKG_FATAL);
			} else {
				pkg_emit_error("duplicate license listing: %s, ignoring", name);
				return (EPKG_OK);
			}
		}
	}

	pkg_get(pkg, PKG_LICENSES, &lic);
	l = ucl_object_fromstring_common(name, strlen(name), 0);
	if (lic == NULL) {
		lic = ucl_object_typed_new(UCL_ARRAY);
		pkg_set(pkg, PKG_LICENSES, lic);
	}
	ucl_array_append(lic, l);

	return (EPKG_OK);
}

int
pkg_adduid(struct pkg *pkg, const char *name, const char *uidstr)
{
	struct pkg_user *u = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->users, name, u);
	if (u != NULL) {
		if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
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
		if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
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
	const char *n1, *v1;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');
	assert(origin != NULL && origin[0] != '\0');
	assert(version != NULL && version[0] != '\0');

	pkg_debug(3, "Pkg: add a new dependency origin: %s, name: %s, version: %s", origin, name, version);
	HASH_FIND_STR(pkg->deps, origin, d);
	if (d != NULL) {
		pkg_get(pkg, PKG_NAME, &n1, PKG_VERSION, &v1);
		if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
			pkg_emit_error("%s-%s: duplicate dependency listing: %s-%s, fatal (developer mode)",
			    n1, v1, name, version);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("%s-%s: duplicate dependency listing: %s-%s, ignoring",
			    n1, v1, name, version);
			return (EPKG_OK);
		}
	}

	pkg_dep_new(&d);

	sbuf_set(&d->origin, origin);
	sbuf_set(&d->name, name);
	sbuf_set(&d->version, version);
	d->locked = locked;

	HASH_ADD_KEYPTR(hh, pkg->deps, pkg_dep_get(d, PKG_DEP_ORIGIN),
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

	pkg_debug(3, "Pkg: add a new reverse dependency origin: %s, name: %s, version: %s", origin, name, version);
	pkg_dep_new(&d);

	sbuf_set(&d->origin, origin);
	sbuf_set(&d->name, name);
	sbuf_set(&d->version, version);
	d->locked = locked;

	HASH_ADD_KEYPTR(hh, pkg->rdeps, pkg_dep_get(d, PKG_DEP_ORIGIN),
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

	pkg_debug(3, "Pkg: add new file '%s'", path);

	if (check_duplicates) {
		HASH_FIND_STR(pkg->files, path, f);
		if (f != NULL) {
			if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
				pkg_emit_error("duplicate file listing: %s, fatal (developer mode)", pkg_file_path(f));
				return (EPKG_FATAL);
			} else {
				pkg_emit_error("duplicate file listing: %s, ignoring", pkg_file_path(f));
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
pkg_addcategory(struct pkg *pkg, const char *name)
{
	const pkg_object *o, *categories;
	pkg_object *c, *cat;
	pkg_iter iter = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	pkg_get(pkg, PKG_CATEGORIES, &categories);
	while ((o = (pkg_object_iterate(categories, &iter)))) {
		if (strcmp(pkg_object_string(o), name) == 0) {
			if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
				pkg_emit_error("duplicate category listing: %s, fatal (developer mode)", name);
				return (EPKG_FATAL);
			} else {
				pkg_emit_error("duplicate category listing: %s, ignoring", name);
				return (EPKG_OK);
			}
		}
	}

	pkg_get(pkg, PKG_CATEGORIES, &cat);
	c = ucl_object_fromstring_common(name, strlen(name), 0);
	if (cat == NULL) {
		cat = ucl_object_typed_new(UCL_ARRAY);
		pkg_set(pkg, PKG_CATEGORIES, cat);
	}
	ucl_array_append(cat, c);

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

	pkg_debug(3, "Pkg: add new directory '%s'", path);
	if (check_duplicates) {
		HASH_FIND_STR(pkg->dirs, path, d);
		if (d != NULL) {
			if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
				pkg_emit_error("duplicate directory listing: %s, fatal (developer mode)", path);
				return (EPKG_FATAL);
			} else {
				pkg_emit_error("duplicate directory listing: %s, ignoring", path);
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
		sbuf_set(&o->key, key);
	} else if ( o->value != NULL) {
		if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
			pkg_emit_error("duplicate options listing: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate options listing: %s, ignoring", key);
			return (EPKG_OK);
		}
	}

	sbuf_set(&o->value, value);
	HASH_ADD_KEYPTR(hh, pkg->options,
			pkg_option_opt(o),
			strlen(pkg_option_opt(o)), o);

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
		sbuf_set(&o->key, key);
	} else if ( o->default_value != NULL) {
		if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
			pkg_emit_error("duplicate default value for option: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate default value for option: %s, ignoring", key);
			return (EPKG_OK);
		}
	}

	sbuf_set(&o->default_value, default_value);
	HASH_ADD_KEYPTR(hh, pkg->options,
			pkg_option_default_value(o),
			strlen(pkg_option_default_value(o)), o);

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
		sbuf_set(&o->key, key);
	} else if ( o->description != NULL) {
		if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
			pkg_emit_error("duplicate description for option: %s, fatal (developer mode)", key);
			return (EPKG_FATAL);
		} else {
			pkg_emit_error("duplicate description for option: %s, ignoring", key);
			return (EPKG_OK);
		}
	}

	sbuf_set(&o->description, description);
	HASH_ADD_KEYPTR(hh, pkg->options,
			pkg_option_description(o),
			strlen(pkg_option_description(o)), o);

	return (EPKG_OK);
}

int
pkg_addshlib_required(struct pkg *pkg, const char *name)
{
	struct pkg_shlib *s = NULL;
	const char *origin;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->shlibs_required, name, s);
	/* silently ignore duplicates in case of shlibs */
	if (s != NULL)
		return (EPKG_OK);

	pkg_shlib_new(&s);

	sbuf_set(&s->name, name);

	HASH_ADD_KEYPTR(hh, pkg->shlibs_required,
	    pkg_shlib_name(s),
	    strlen(pkg_shlib_name(s)), s);

	pkg_get(pkg, PKG_ORIGIN, &origin);
	pkg_debug(3, "added shlib deps for %s on %s",
			origin, name);

	return (EPKG_OK);
}

int
pkg_addshlib_provided(struct pkg *pkg, const char *name)
{
	struct pkg_shlib *s = NULL;
	const char *origin;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->shlibs_provided, name, s);
	/* silently ignore duplicates in case of shlibs */
	if (s != NULL)
		return (EPKG_OK);

	pkg_shlib_new(&s);

	sbuf_set(&s->name, name);

	HASH_ADD_KEYPTR(hh, pkg->shlibs_provided,
	    pkg_shlib_name(s),
	    strlen(pkg_shlib_name(s)), s);

	pkg_get(pkg, PKG_ORIGIN, &origin);
	pkg_debug(3, "added shlib provide %s for %s",
			name, origin);

	return (EPKG_OK);
}

int
pkg_addconflict(struct pkg *pkg, const char *name)
{
	struct pkg_conflict *c = NULL;
	const char *origin;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	HASH_FIND_STR(pkg->conflicts, __DECONST(char *, name), c);
	/* silently ignore duplicates in case of conflicts */
	if (c != NULL)
		return (EPKG_OK);

	pkg_conflict_new(&c);
	sbuf_set(&c->origin, name);
	pkg_get(pkg, PKG_ORIGIN, &origin);
	pkg_debug(3, "Pkg: add a new conflict origin: %s, with %s", origin, name);

	HASH_ADD_KEYPTR(hh, pkg->conflicts,
	    __DECONST(char *, pkg_conflict_origin(c)),
	    sbuf_size(c->origin), c);

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
	sbuf_set(&p->provide, name);

	HASH_ADD_KEYPTR(hh, pkg->provides,
	    __DECONST(char *, pkg_provide_name(p)),
	    sbuf_size(p->provide), p);

	return (EPKG_OK);
}

int
pkg_addannotation(struct pkg *pkg, const char *tag, const char *value)
{
	const ucl_object_t *an, *notes;
	ucl_object_t *o, *annotations;

	assert(pkg != NULL);
	assert(tag != NULL);
	assert(value != NULL);

	/* Tags are unique per-package */

	pkg_get(pkg, PKG_ANNOTATIONS, &notes);
	an = pkg_object_find(notes, tag);
	if (an != NULL) {
		if (pkg_object_bool(pkg_config_get("DEVELOPER_MODE"))) {
			pkg_emit_error("duplicate annotation tag: %s value: %s,"
			    " fatal (developer mode)", tag, value);
			return (EPKG_OK);
		} else {
			pkg_emit_error("duplicate annotation tag: %s value: %s,"
			    " ignoring", tag, value);
			return (EPKG_OK);
		}
	}
	o = ucl_object_fromstring_common(value, strlen(value), 0);
	pkg_get(pkg, PKG_ANNOTATIONS, &annotations);
	if (annotations == NULL) {
		annotations = ucl_object_typed_new(UCL_OBJECT);
		pkg_set(pkg, PKG_ANNOTATIONS, annotations);
	}
	ucl_object_insert_key(annotations, o, tag, strlen(tag), true);

	return (EPKG_OK);
}

int
pkg_delannotation(struct pkg *pkg, const char *tag)
{
	ucl_object_t *an, *notes;

	assert(pkg != NULL);
	assert(tag != NULL);

	pkg_get(pkg, PKG_ANNOTATIONS, &notes);
	an = ucl_object_pop_keyl(notes, tag, strlen(tag));
	if (an != NULL) {
		ucl_object_unref(an);
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
		HASH_FREE(pkg->files, pkg_file_free);
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
	const void	*buf;
	size_t		 size;
	off_t		 offset = 0;
	struct sbuf	*sbuf;
	int		 i, r;
	bool		 read_from_stdin = 0;

	struct {
		const char *name;
		pkg_attr attr;
	} files[] = {
		{ "+MTREE_DIRS", PKG_MTREE },
		{ NULL, 0 }
	};

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
			pkg_emit_error("archive_read_open_filename(%s): %s", path,
			    archive_error_string(*a));
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	} else {
		if (archive_read_open_fd(*a, fd, 4096) != ARCHIVE_OK) {
			pkg_emit_error("archive_read_open_fd: %s",
			    archive_error_string(*a));
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}

	if (*pkg_p == NULL) {
		retcode = pkg_new(pkg_p, PKG_FILE);
		if (retcode != EPKG_OK)
			goto cleanup;
	} else
		pkg_reset(*pkg_p, PKG_FILE);

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
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			if (flags & PKG_OPEN_MANIFEST_ONLY)
				break;
		}

		for (i = 0; files[i].name != NULL; i++) {
			if (strcmp(fpath, files[i].name) == 0) {
				sbuf = sbuf_new_auto();
				offset = 0;
				for (;;) {
					if ((r = archive_read_data_block(*a, &buf,
							&size, &offset)) == 0) {
						sbuf_bcat(sbuf, buf, size);
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
				sbuf_finish(sbuf);
				pkg_set(pkg, PKG_MTREE, sbuf_data(sbuf));
				sbuf_delete(sbuf);
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
pkg_copy_tree(struct pkg *pkg, const char *src, const char *dest)
{
	struct packing *pack;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	char spath[MAXPATHLEN];
	char dpath[MAXPATHLEN];
	const char *prefix;
	char *mtree;
	const pkg_object *o;

	o = pkg_config_get("DISABLE_MTREE");
	if (o && !pkg_object_bool(o)) {
		pkg_get(pkg, PKG_PREFIX, &prefix, PKG_MTREE, &mtree);
		do_extract_mtree(mtree, prefix);
	}

	/* Execute pre-install scripts */
	pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL);

	if (packing_init(&pack, dest, 0) != EPKG_OK) {
		/* TODO */
		return EPKG_FATAL;
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		snprintf(spath, sizeof(spath), "%s%s", src, pkg_dir_path(dir));
		snprintf(dpath, sizeof(dpath), "%s%s", dest, pkg_dir_path(dir));
		packing_append_file_attr(pack, spath, dpath,
		    dir->uname, dir->gname, dir->perm);
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		snprintf(spath, sizeof(spath), "%s%s", src, pkg_file_path(file));
		snprintf(dpath, sizeof(dpath), "%s%s", dest, pkg_file_path(file));
		packing_append_file_attr(pack, spath, dpath,
		    file->uname, file->gname, file->perm);
	}

	/* Execute post-install scripts */
	pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL);

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
pkg_has_message(struct pkg *p)
{
	const char *msg;

	pkg_get(p, PKG_MESSAGE, &msg);

	if (msg != NULL)
		return (true);

	return (false);
}

bool
pkg_is_locked(const struct pkg * restrict p)
{
	bool ret;

	assert(p != NULL);

	pkg_get(p, PKG_LOCKED, &ret);

	return (ret);
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
