/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2017 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023, Serenity Cyber Security, LLC
 *                     Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
#include "xmalloc.h"

#define dbg(x, ...) pkg_dbg(PKG_DBG_PACKAGE, x, __VA_ARGS__)

int
pkg_new(struct pkg **pkg, pkg_t type)
{
	*pkg = xcalloc(1, sizeof(struct pkg));
	(*pkg)->type = type;
	(*pkg)->rootfd = -1;
	(*pkg)->list_sorted = false;

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
	free(pkg->version);
	free(pkg->maintainer);
	free(pkg->www);
	free(pkg->altabi);
	free(pkg->abi);
	free(pkg->uid);
	free(pkg->digest);
	free(pkg->old_digest);
	free(pkg->prefix);
	free(pkg->oprefix);
	free(pkg->comment);
	free(pkg->desc);
	free(pkg->sum);
	free(pkg->repopath);
	free(pkg->reponame);
	free(pkg->repourl);
	free(pkg->reason);
	free(pkg->dep_formula);

	for (int i = 0; i < PKG_NUM_SCRIPTS; i++)
		xstring_free(pkg->scripts[i]);
	for (int i = 0; i < PKG_NUM_LUA_SCRIPTS; i++)
		vec_free_and_free(&pkg->lua_scripts[i], free);

	pkg_list_free(pkg, PKG_DEPS);
	pkg_list_free(pkg, PKG_RDEPS);
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg_list_free(pkg, PKG_OPTIONS);
	pkg_list_free(pkg, PKG_CONFIG_FILES);

	vec_free_and_free(&pkg->users, free);
	pkg->flags &= ~PKG_LOAD_USERS;
	vec_free_and_free(&pkg->groups, free);
	pkg->flags &= ~PKG_LOAD_GROUPS;
	vec_free_and_free(&pkg->shlibs_required, free);
	pkg->flags &= ~PKG_LOAD_SHLIBS_REQUIRED;
	vec_free_and_free(&pkg->shlibs_provided, free);
	pkg->flags &= ~PKG_LOAD_SHLIBS_REQUIRED;
	vec_free_and_free(&pkg->provides, free);
	pkg->flags &= ~PKG_LOAD_PROVIDES;
	vec_free_and_free(&pkg->requires, free);
	pkg->flags &= ~PKG_LOAD_REQUIRES;
	vec_free_and_free(&pkg->categories, free);
	pkg->flags &= ~PKG_LOAD_CATEGORIES;
	vec_free_and_free(&pkg->licenses, free);
	pkg->flags &= ~PKG_LOAD_LICENSES;

	vec_free_and_free(&pkg->message, pkg_message_free);
	vec_free_and_free(&pkg->annotations, pkg_kv_free);

	vec_free_and_free(&pkg->dir_to_del, free);

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

int
pkg_set_i(struct pkg *pkg, pkg_attr attr, int64_t val)
{
	switch (attr) {
	case PKG_ATTR_FLATSIZE:
		pkg->flatsize = val;
		break;
	case PKG_ATTR_OLD_FLATSIZE:
		pkg->old_flatsize = val;
		break;
	case PKG_ATTR_PKGSIZE:
		pkg->pkgsize = val;
		break;
	case PKG_ATTR_TIME:
		pkg->timestamp = val;
		break;
	default:
		pkg_emit_error("%d does not accept int64_t values", attr);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

int
pkg_set_b(struct pkg *pkg, pkg_attr attr, bool boolean)
{
	switch (attr) {
	case PKG_ATTR_AUTOMATIC:
		pkg->automatic = boolean;
		break;
	case PKG_ATTR_LOCKED:
		pkg->locked = boolean;
		break;
	case PKG_ATTR_VITAL:
		pkg->vital = boolean;
		break;
	default:
		pkg_emit_error("%d does not accept bool values", attr);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

int
pkg_set_s(struct pkg *pkg, pkg_attr attr, const char *str)
{
	char *endptr;
	ucl_object_t *obj;
	int64_t i;

	switch (attr) {
	case PKG_ATTR_NAME:
		free(pkg->name);
		pkg->name = xstrdup(str);
		free(pkg->uid);
		pkg->uid = xstrdup(str);
		break;
	case PKG_ATTR_ORIGIN:
		free(pkg->origin);
		pkg->origin = xstrdup(str);
		break;
	case PKG_ATTR_VERSION:
		free(pkg->version);
		pkg->version = xstrdup(str);
		break;
	case PKG_ATTR_DESC:
		free(pkg->desc);
		pkg->desc = xstrdup(str);
		break;
	case PKG_ATTR_COMMENT:
		free(pkg->comment);
		pkg->comment = xstrdup(str);
		break;
	case PKG_ATTR_MESSAGE:
		vec_free_and_free(&pkg->message, pkg_message_free);
		if (*str == '[') {
			pkg_message_from_str(pkg, str, strlen(str));
		} else {
			obj = ucl_object_fromstring_common(str, strlen(str),
			    UCL_STRING_RAW|UCL_STRING_TRIM);
			pkg_message_from_ucl(pkg, obj);
			ucl_object_unref(obj);
		}
		break;
	case PKG_ATTR_ARCH:
		free(pkg->altabi);
		pkg->altabi = xstrdup(str);
		break;
	case PKG_ATTR_ABI:
		free(pkg->abi);
		pkg->abi = xstrdup(str);
		break;
	case PKG_ATTR_MAINTAINER:
		free(pkg->maintainer);
		pkg->maintainer = xstrdup(str);
		break;
	case PKG_ATTR_WWW:
		free(pkg->www);
		pkg->www = xstrdup(str);
		break;
	case PKG_ATTR_PREFIX:
		free(pkg->prefix);
		pkg->prefix = xstrdup(str);
		break;
	case PKG_ATTR_REPOPATH:
		free(pkg->repopath);
		pkg->repopath = xstrdup(str);
		break;
	case PKG_ATTR_CKSUM:
		free(pkg->sum);
		pkg->sum = xstrdup(str);
		break;
	case PKG_ATTR_OLD_VERSION:
		free(pkg->old_version);
		pkg->old_version = xstrdup(str);
		break;
	case PKG_ATTR_REPONAME:
		free(pkg->reponame);
		pkg->reponame = xstrdup(str);
		break;
	case PKG_ATTR_REPOURL:
		free(pkg->repourl);
		pkg->repourl = xstrdup(str);
		break;
	case PKG_ATTR_DIGEST:
		free(pkg->digest);
		pkg->digest = xstrdup(str);
		break;
	case PKG_ATTR_REASON:
		free(pkg->reason);
		pkg->reason = xstrdup(str);
		break;
	case PKG_ATTR_DEP_FORMULA:
		free(pkg->dep_formula);
		pkg->dep_formula = xstrdup(str);
		break;
	case PKG_ATTR_FLATSIZE:
		i = strtoimax(str, &endptr, 10);
		if (endptr != NULL) {
			pkg_emit_error("Impossible to convert '%s' to int64_t",
			    str);
			return (EPKG_FATAL);
		}
		pkg->flatsize = i;
		break;
	case PKG_ATTR_OLD_FLATSIZE:
		i = strtoimax(str, &endptr, 10);
		if (endptr != NULL) {
			pkg_emit_error("Impossible to convert '%s' to int64_t",
			    str);
			return (EPKG_FATAL);
		}
		pkg->old_flatsize = i;
		break;
	case PKG_ATTR_PKGSIZE:
		i = strtoimax(str, &endptr, 10);
		if (endptr != NULL) {
			pkg_emit_error("Impossible to convert '%s' to int64_t",
			    str);
			return (EPKG_FATAL);
		}
		pkg->pkgsize = i;
		break;
	case PKG_ATTR_TIME:
		i = strtoimax(str, &endptr, 10);
		if (endptr != NULL) {
			pkg_emit_error("Impossible to convert '%s' to int64_t",
			    str);
			return (EPKG_FATAL);
		}
		pkg->timestamp = i;
		break;
	default:
		pkg_emit_error("%d does not accept string values", attr);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
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
pkg_each(config_files, struct pkg_config_file, config_files);

int
pkg_adduser(struct pkg *pkg, const char *name)
{
	return (pkg_addstring(&pkg->users, name, "user"));
}

int
pkg_addgroup(struct pkg *pkg, const char *name)
{
	return (pkg_addstring(&pkg->groups, name, "group"));
}

int
pkg_adddep(struct pkg *pkg, const char *name, const char *origin, const char *version, bool locked)
{
	if (pkg_adddep_chain(NULL, pkg, name, origin, version, locked) == NULL) {
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

struct pkg_dep *
pkg_adddep_chain(struct pkg_dep *chain,
		struct pkg *pkg,
		const char *name,
		const char *origin,
		const char *version, bool locked)
{
	struct pkg_dep *d = NULL;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	dbg(3, "add a new dependency origin: %s, name: %s", origin ? origin : "", name);
	if (pkghash_get(pkg->depshash, name) != NULL) {
		pkg_emit_error("%s: duplicate dependency listing: %s",
		    pkg->name, name);
		return (NULL);
	}

	d = xcalloc(1, sizeof(*d));
	if (origin != NULL && origin[0] != '\0')
		d->origin = xstrdup(origin);
	d->name = xstrdup(name);
	if (version != NULL && version[0] != '\0')
		d->version = xstrdup(version);
	d->uid = xstrdup(name);
	d->locked = locked;

	pkghash_safe_add(pkg->depshash, d->name, d, NULL);
	if (chain == NULL) {
		DL_APPEND(pkg->depends, d);
		chain = pkg->depends;
	}
	else {
		DL_APPEND2(chain, d, alt_prev, alt_next);
	}

	return (chain);
}

int
pkg_addrdep(struct pkg *pkg, const char *name, const char *origin, const char *version, bool locked)
{
	struct pkg_dep *d;

	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	dbg(3, "add a new reverse dependency origin: %s, name: %s", origin ? origin : "", name);

	d = xcalloc(1, sizeof(*d));
	if (origin != NULL && origin[0] != '\0')
		d->origin = xstrdup(origin);
	d->name = xstrdup(name);
	if (version != NULL && version[0] != '\0')
		d->version = xstrdup(version);
	d->uid = xstrdup(name);
	d->locked = locked;

	pkghash_safe_add(pkg->rdepshash, d->name, d, NULL);
	LL_PREPEND(pkg->rdepends, d);

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
	dbg(3, "add new file '%s'", path);

	if (check_duplicates && pkghash_get(pkg->filehash, path) != NULL) {
		if (ctx.developer_mode) {
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

	pkghash_safe_add(pkg->filehash, f->path, f, NULL);
	DL_APPEND(pkg->files, f);

	return (EPKG_OK);
}

int
pkg_addconfig_file(struct pkg *pkg, const char *path, const char *content)
{
	struct pkg_config_file *f = NULL;
	char abspath[MAXPATHLEN];

	path = pkg_absolutepath(path, abspath, sizeof(abspath), false);
	dbg(3, "add new config file '%s'", path);

	if (pkghash_get(pkg->config_files_hash, path) != NULL) {
		pkg_emit_error("duplicate file listing: %s", path);
		return (EPKG_FATAL);
	}
	f = xcalloc(1, sizeof(*f));
	strlcpy(f->path, path, sizeof(f->path));

	if (content != NULL)
		f->content = xstrdup(content);

	pkghash_safe_add(pkg->config_files_hash, f->path, f, NULL);
	DL_APPEND(pkg->config_files, f);

	return (EPKG_OK);
}

int
pkg_addstring(charv_t *list, const char *val, const char *title)
{
	assert(val != NULL);
	assert(title != NULL);

	if (charv_contains(list, val, false)) {
		if (ctx.developer_mode) {
			pkg_emit_error("duplicate %s listing: %s, fatal"
			    " (developer mode)", title, val);
			return (EPKG_FATAL);
		}
		pkg_emit_error("duplicate %s listing: %s, "
		    "ignoring", title, val);
		return (EPKG_OK);
	}

	vec_push(list, xstrdup(val));

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

	if (STREQ(path, "/")) {
		pkg_emit_error("skipping useless directory: '%s'\n", path);
		return (EPKG_OK);
	}
	path = pkg_absolutepath(path, abspath, sizeof(abspath), false);
	dbg(3, "add new directory '%s'", path);
	if (check_duplicates && pkghash_get(pkg->dirhash, path) != NULL) {
		if (ctx.developer_mode) {
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

	pkghash_safe_add(pkg->dirhash, d->path, d, NULL);
	DL_APPEND(pkg->dirs, d);

	return (EPKG_OK);
}

int
pkg_addscript(struct pkg *pkg, const char *data, pkg_script type)
{

	assert(pkg != NULL);
	xstring_renew(pkg->scripts[type]);
	fprintf(pkg->scripts[type]->fp, "%s", data);

	return (EPKG_OK);
}

int
pkg_add_lua_script(struct pkg *pkg, const char *data, pkg_lua_script type)
{
	assert(pkg != NULL);

	if (type >= PKG_LUA_UNKNOWN)
		return (EPKG_FATAL);

	vec_push(&pkg->lua_scripts[type], xstrdup(data));

	return (EPKG_OK);
}

int
pkg_addluascript_fileat(int fd, struct pkg *pkg, const char *filename)
{
	char *data;
	pkg_lua_script type;
	int ret = EPKG_OK;
	off_t sz = 0;

	assert(pkg != NULL);
	assert(filename != NULL);

	dbg(1, "Adding script from: '%s'", filename);

	if ((ret = file_to_bufferat(fd, filename, &data, &sz)) != EPKG_OK)
		return (ret);

	if (STREQ(filename, "pkg-pre-install.lua")) {
		type = PKG_LUA_PRE_INSTALL;
	} else if (STREQ(filename, "pkg-post-install.lua")) {
		type = PKG_LUA_POST_INSTALL;
	} else if (STREQ(filename, "pkg-pre-deinstall.lua")) {
		type = PKG_LUA_PRE_DEINSTALL;
	} else if (STREQ(filename, "pkg-post-deinstall.lua")) {
		type = PKG_LUA_POST_DEINSTALL;
	} else {
		pkg_emit_error("unknown lua script '%s'", filename);
		ret = EPKG_FATAL;
		goto cleanup;
	}

	ret = pkg_add_lua_script(pkg, data, type);
cleanup:
	free(data);
	return (ret);
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

	dbg(1, "Adding script from: '%s'", filename);

	if ((ret = file_to_bufferat(fd, filename, &data, &sz)) != EPKG_OK)
		return (ret);

	if (STREQ(filename, "pkg-pre-install") ||
			STREQ(filename, "+PRE_INSTALL")) {
		type = PKG_SCRIPT_PRE_INSTALL;
	} else if (STREQ(filename, "pkg-post-install") ||
			STREQ(filename, "+POST_INSTALL")) {
		type = PKG_SCRIPT_POST_INSTALL;
	} else if (STREQ(filename, "pkg-install") ||
			STREQ(filename, "+INSTALL")) {
		type = PKG_SCRIPT_INSTALL;
	} else if (STREQ(filename, "pkg-pre-deinstall") ||
			STREQ(filename, "+PRE_DEINSTALL")) {
		type = PKG_SCRIPT_PRE_DEINSTALL;
	} else if (STREQ(filename, "pkg-post-deinstall") ||
			STREQ(filename, "+POST_DEINSTALL")) {
		type = PKG_SCRIPT_POST_DEINSTALL;
	} else if (STREQ(filename, "pkg-deinstall") ||
			STREQ(filename, "+DEINSTALL")) {
		type = PKG_SCRIPT_DEINSTALL;
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
		pkg->scripts[type] = xstring_new();

	fprintf(pkg->scripts[type]->fp, "%s", cmd);

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

	dbg(2,"adding options: %s = %s", key, value);
	if (pkghash_get(pkg->optionshash, key) != NULL) {
		if (ctx.developer_mode) {
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
	pkghash_safe_add(pkg->optionshash, o->key, o, NULL);
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

	if (pkghash_get(pkg->optionshash, key) != NULL) {
		if (ctx.developer_mode) {
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
	pkghash_safe_add(pkg->optionshash, o->key, o, NULL);
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

	if (pkghash_get(pkg->optionshash, key) != NULL) {
		if (ctx.developer_mode) {
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
	pkghash_safe_add(pkg->optionshash, o->key, o, NULL);
	DL_APPEND(pkg->options, o);

	return (EPKG_OK);
}

enum pkg_shlib_flags
pkg_shlib_flags_from_abi(const struct pkg_abi *shlib_abi)
{
	enum pkg_shlib_flags flags = PKG_SHLIB_FLAGS_NONE;

	if (ctx.abi.os == PKG_OS_FREEBSD) {
		if (shlib_abi->os == PKG_OS_LINUX && ctx.track_linux_compat_shlibs)
			flags |= PKG_SHLIB_FLAGS_COMPAT_LINUX;

		switch (ctx.abi.arch) {
		case PKG_ARCH_AMD64:
			if (shlib_abi->arch == PKG_ARCH_I386) {
				flags |= PKG_SHLIB_FLAGS_COMPAT_32;
			}
			break;
		case PKG_ARCH_AARCH64:
			if (shlib_abi->arch == PKG_ARCH_ARMV7) {
				flags |= PKG_SHLIB_FLAGS_COMPAT_32;
			}
			break;
		case PKG_ARCH_POWERPC64:
			if (shlib_abi->arch == PKG_ARCH_POWERPC) {
				flags |= PKG_SHLIB_FLAGS_COMPAT_32;
			}
			break;
		}
	}

	return (flags);
}

/*
 * Format examples:
 *
 * libfoo.so.1.0.0          - native
 * libfoo.so.1.0.0:32       - compat 32
 * libfoo.so.1.0.0:Linux    - compat Linux
 * libfoo.so.1.0.0:Linux:32 - compat Linux 32
 */
char *
pkg_shlib_name_with_flags(const char *name, enum pkg_shlib_flags flags)
{
	const char *compat_os = "";
	if ((flags & PKG_SHLIB_FLAGS_COMPAT_LINUX) != 0) {
		compat_os = ":Linux";
	}

	const char *compat_arch = "";
	if ((flags & PKG_SHLIB_FLAGS_COMPAT_32) != 0) {
		compat_arch = ":32";
	}

	char *ret;
	xasprintf(&ret, "%s%s%s", name, compat_os, compat_arch);
	return (ret);
}

int
pkg_addshlib_required(struct pkg *pkg, const char *name,
    enum pkg_shlib_flags flags)
{
	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	char *full_name = pkg_shlib_name_with_flags(name, flags);

	/* silently ignore duplicates in case of shlibs */
	if (charv_contains(&pkg->shlibs_required, full_name, false)) {
		free(full_name);
		return (EPKG_OK);
	}

	vec_push(&pkg->shlibs_required, full_name);

	dbg(3, "added shlib deps for %s on %s", pkg->name, full_name);

	return (EPKG_OK);
}

int
pkg_addshlib_provided(struct pkg *pkg, const char *name,
    enum pkg_shlib_flags flags)
{
	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	char *full_name = pkg_shlib_name_with_flags(name, flags);

	/* silently ignore duplicates in case of shlibs */
	if (charv_contains(&pkg->shlibs_provided, full_name, false)) {
		free(full_name);
		return (EPKG_OK);
	}

	vec_push(&pkg->shlibs_provided, full_name);

	dbg(3, "added shlib provide %s for %s", full_name, pkg->name);

	return (EPKG_OK);
}

int
pkg_addconflict(struct pkg *pkg, const char *uniqueid)
{
	struct pkg_conflict *c = NULL;

	assert(pkg != NULL);
	assert(uniqueid != NULL && uniqueid[0] != '\0');

	if (pkghash_get(pkg->conflictshash, uniqueid) != NULL) {
		/* silently ignore duplicates in case of conflicts */
		return (EPKG_OK);
	}

	c = xcalloc(1, sizeof(*c));
	c->uid = xstrdup(uniqueid);
	dbg(3, "add a new conflict origin: %s, with %s", pkg->uid, uniqueid);

	pkghash_safe_add(pkg->conflictshash, c->uid, c, NULL);
	DL_APPEND(pkg->conflicts, c);

	return (EPKG_OK);
}

int
pkg_addrequire(struct pkg *pkg, const char *name)
{
	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	/* silently ignore duplicates in case of conflicts */
	if (charv_contains(&pkg->requires, name, false))
		return (EPKG_OK);

	vec_push(&pkg->requires, xstrdup(name));

	return (EPKG_OK);
}

int
pkg_addprovide(struct pkg *pkg, const char *name)
{
	assert(pkg != NULL);
	assert(name != NULL && name[0] != '\0');

	/* silently ignore duplicates in case of conflicts */
	if (charv_contains(&pkg->provides, name, false))
		return (EPKG_OK);

	vec_push(&pkg->provides, xstrdup(name));

	return (EPKG_OK);
}

const char *
pkg_kv_get(const kvlist_t *kv, const char *tag)
{
	assert(tag != NULL);

	vec_foreach(*kv, i) {
		if (STREQ(kv->d[i]->key, tag))
			return (kv->d[i]->value);
	}

	return (NULL);
}

int
pkg_kv_add(kvlist_t *list, const char *key, const char *val, const char *title)
{
	struct pkg_kv *kv;

	assert(val != NULL);
	assert(title != NULL);

	vec_foreach(*list, i) {
		if (!STREQ(list->d[i]->key, key))
			continue;
		if (ctx.developer_mode) {
			pkg_emit_error("duplicate %s: %s, fatal"
				    " (developer mode)", title, key);
				return (EPKG_FATAL);
		}
		pkg_emit_error("duplicate %s: %s, "
		    "ignoring", title, val);
		return (EPKG_OK);
	}

	kv = pkg_kv_new(key, val);
	vec_push(list, kv);

	return (EPKG_OK);
}

int
pkg_list_count(const struct pkg *pkg, pkg_list list)
{
	switch (list) {
	case PKG_DEPS:
		return (pkghash_count(pkg->depshash));
	case PKG_RDEPS:
		return (pkghash_count(pkg->rdepshash));
	case PKG_OPTIONS:
		return (pkghash_count(pkg->optionshash));
	case PKG_FILES:
		return (pkghash_count(pkg->filehash));
	case PKG_DIRS:
		return (pkghash_count(pkg->dirhash));
	case PKG_CONFLICTS:
		return (pkghash_count(pkg->conflictshash));
	case PKG_CONFIG_FILES:
		return (pkghash_count(pkg->config_files_hash));
	case PKG_USERS:
		return (vec_len(&pkg->users));
	case PKG_GROUPS:
		return (vec_len(&pkg->groups));
	case PKG_SHLIBS_REQUIRED:
		return (vec_len(&pkg->shlibs_required));
	case PKG_SHLIBS_PROVIDED:
		return (vec_len(&pkg->shlibs_provided));
	case PKG_REQUIRES:
		return (vec_len(&pkg->requires));
	case PKG_PROVIDES:
		return (vec_len(&pkg->provides));
	}

	return (0);
}

void
pkg_list_free(struct pkg *pkg, pkg_list list)  {
	struct pkg_dep *cur;

	switch (list) {
	case PKG_DEPS:
		DL_FOREACH (pkg->depends, cur) {
			if (cur->alt_next) {
				DL_FREE2(cur->alt_next, pkg_dep_free, alt_prev, alt_next);
			}
		}
		DL_FREE(pkg->depends, pkg_dep_free);
		pkghash_destroy(pkg->depshash);
		pkg->depshash = NULL;
		pkg->flags &= ~PKG_LOAD_DEPS;
		break;
	case PKG_RDEPS:
		LL_FREE(pkg->rdepends, pkg_dep_free);
		pkghash_destroy(pkg->rdepshash);
		pkg->depshash = NULL;
		pkg->flags &= ~PKG_LOAD_RDEPS;
		break;
	case PKG_OPTIONS:
		DL_FREE(pkg->options, pkg_option_free);
		pkghash_destroy(pkg->optionshash);
		pkg->optionshash = NULL;
		pkg->flags &= ~PKG_LOAD_OPTIONS;
		break;
	case PKG_FILES:
	case PKG_CONFIG_FILES:
		DL_FREE(pkg->files, pkg_file_free);
		pkghash_destroy(pkg->filehash);
		pkg->filehash = NULL;
		DL_FREE(pkg->config_files, pkg_config_file_free);
		pkghash_destroy(pkg->config_files_hash);
		pkg->config_files_hash = NULL;
		pkg->flags &= ~PKG_LOAD_FILES;
		break;
	case PKG_DIRS:
		DL_FREE(pkg->dirs, free);
		pkghash_destroy(pkg->dirhash);
		pkg->dirhash = NULL;
		pkg->flags &= ~PKG_LOAD_DIRS;
		break;
	case PKG_CONFLICTS:
		DL_FREE(pkg->conflicts, pkg_conflict_free);
		pkghash_destroy(pkg->conflictshash);
		pkg->conflictshash = NULL;
		pkg->flags &= ~PKG_LOAD_CONFLICTS;
		break;
	}
}

int
pkg_open(struct pkg **pkg_p, const char *path, int flags)
{
	struct archive *a;
	struct archive_entry *ae;
	int ret;

	ret = pkg_open2(pkg_p, &a, &ae, path, flags, -1);

	if (ret != EPKG_OK && ret != EPKG_END)
		return (EPKG_FATAL);

	archive_read_close(a);
	archive_read_free(a);

	return (EPKG_OK);
}

int
pkg_open_fd(struct pkg **pkg_p, int fd, int flags)
{
	struct archive *a;
	struct archive_entry *ae;
	int ret;

	ret = pkg_open2(pkg_p, &a, &ae, NULL, flags, fd);

	if (ret != EPKG_OK && ret != EPKG_END)
		return (EPKG_FATAL);

	archive_read_close(a);
	archive_read_free(a);

	return (EPKG_OK);
}

static int
pkg_parse_archive(struct pkg *pkg, struct archive *a, size_t len)
{
	void *buffer;
	int rc;

	buffer = xmalloc(len);

	archive_read_data(a, buffer, len);
	rc = pkg_parse_manifest(pkg, buffer, len);
	free(buffer);
	return (rc);
}

int
pkg_open2(struct pkg **pkg_p, struct archive **a, struct archive_entry **ae,
    const char *path, int flags, int fd)
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
		if (path == NULL) {
			pkg_emit_error("bad usage of pkg_open2");
			retcode = EPKG_FATAL;
			goto cleanup;
		}
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
			STREQ(fpath, "+COMPACT_MANIFEST")) {
			manifest = true;

			ret = pkg_parse_archive(pkg, *a, archive_entry_size(*ae));
			if (ret != EPKG_OK) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			/* Do not read anything more */
			break;
		}
		if (!manifest && STREQ(fpath, "+MANIFEST")) {
			manifest = true;

			ret = pkg_parse_archive(pkg, *a, archive_entry_size(*ae));
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
			return (pkg_checksum_calculate(pkg, db, false, true, false));
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
		if (f->sum != NULL &&
		    /* skip config files as they can be modified */
		    pkghash_get_value(pkg->config_files_hash, f->path) == NULL) {
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
pkg_try_installed(struct pkgdb *db, const char *name,
		struct pkg **pkg, unsigned flags) {
	struct pkgdb_it *it = NULL;
	int ret = EPKG_FATAL;

	if ((it = pkgdb_query(db, name, MATCH_INTERNAL)) == NULL)
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
	return (vec_len(&p->message) > 0);
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

	if (pkghash_count(p->config_files_hash) == 0)
		return (false);

	*file = pkghash_get_value(p->filehash, path);
	if (*file == NULL)
		return (false);
	*cfile = pkghash_get_value(p->config_files_hash, path);
	if (*cfile == NULL) {
		*file = NULL;
		return (false);
	}

	return (true);
}

struct pkg_dir *
pkg_get_dir(struct pkg *p, const char *path)
{
	return (pkghash_get_value(p->dirhash, path));
}

struct pkg_file *
pkg_get_file(struct pkg *p, const char *path)
{
	return (pkghash_get_value(p->filehash, path));
}

bool
pkg_has_file(struct pkg *p, const char *path)
{
	return (pkghash_get(p->filehash, path) != NULL);
}

bool
pkg_has_dir(struct pkg *p, const char *path)
{
	return (pkghash_get(p->dirhash, path) != NULL);
}

int
pkg_open_root_fd(struct pkg *pkg)
{
	const char *path;

	if (pkg->rootfd != -1)
		return (EPKG_OK);

	path = pkg_kv_get(&pkg->annotations, "relocated");
	if (path == NULL) {
		if ((pkg->rootfd = dup(ctx.rootfd)) == -1) {
			pkg_emit_errno("dup", "rootfd");
			return (EPKG_FATAL);
		}
		return (EPKG_OK);
	}

	pkg_absolutepath(path, pkg->rootpath, sizeof(pkg->rootpath), false);

	if ((pkg->rootfd = openat(ctx.rootfd, pkg->rootpath + 1, O_DIRECTORY)) >= 0 )
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
		vec_push(&pkg->message, msg);
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
			if (STRIEQ(ucl_object_tostring(elt), "install"))
				msg->type = PKG_MESSAGE_INSTALL;
			else if (STRIEQ(ucl_object_tostring(elt), "remove"))
				msg->type = PKG_MESSAGE_REMOVE;
			else if (STRIEQ(ucl_object_tostring(elt), "upgrade"))
				msg->type = PKG_MESSAGE_UPGRADE;
			else
				pkg_emit_error("Unknown message type,"
				    " message will always be printed");
		}
		if (msg->type != PKG_MESSAGE_UPGRADE) {
			vec_push(&pkg->message, msg);
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

		vec_push(&pkg->message, msg);
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
	if (pkg->prefix != NULL) {
		ucl_parser_register_variable(parser, "PREFIX", pkg->prefix);
	}
	if (pkg->name != NULL) {
		ucl_parser_register_variable(parser, "PKGNAME", pkg->name);
	}
	if (pkg->origin != NULL) {
		ucl_parser_register_variable(parser, "PKGORIGIN", pkg->origin);
	}
	if (pkg->maintainer != NULL) {
		ucl_parser_register_variable(parser, "MAINTAINER", pkg->maintainer);
	}

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
	vec_foreach(pkg->message, i) {
		msg = pkg->message.d[i];
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

	if (vec_len(&pkg->message) <= 0)
		return (NULL);

	obj = pkg_message_to_ucl(pkg);
	ret = ucl_object_emit(obj, UCL_EMIT_JSON_COMPACT);
	ucl_object_unref(obj);

	return (ret);
}

static int
pkg_dep_cmp(struct pkg_dep *a, struct pkg_dep *b)
{
	return (STREQ(a->name, b->name));
}

static int
pkg_file_cmp(struct pkg_file *a, struct pkg_file *b)
{
	return (STREQ(a->path, b->path));
}

static int
pkg_dir_cmp(struct pkg_dir *a, struct pkg_dir *b)
{
	return (STREQ(a->path, b->path));
}

static int
pkg_option_cmp(struct pkg_option *a, struct pkg_option *b)
{
	return (STREQ(a->key, b->key));
}

static int
pkg_cf_cmp(struct pkg_config_file *a, struct pkg_config_file *b)
{
	return (STREQ(a->path, b->path));
}

static int
char_cmp(const void *a, const void *b) {
    return strcmp(*(char **)a, *(char **)b);
}

void
pkg_lists_sort(struct pkg *p)
{
	if (p->list_sorted)
		return;
	p->list_sorted = true;

	if (p->categories.d)
		qsort(p->categories.d, p->categories.len, sizeof(char *), char_cmp);
	if (p->licenses.d)
		qsort(p->licenses.d, p->licenses.len, sizeof(char *), char_cmp);
	if (p->users.d)
		qsort(p->users.d, p->users.len, sizeof(char *), char_cmp);
	if (p->groups.d)
		qsort(p->groups.d, p->groups.len, sizeof(char *), char_cmp);
	if (p->shlibs_required.d)
		qsort(p->shlibs_required.d, p->shlibs_required.len, sizeof(char *), char_cmp);
	if (p->shlibs_provided.d)
		qsort(p->shlibs_provided.d, p->shlibs_provided.len, sizeof(char *), char_cmp);
	if (p->provides.d)
		qsort(p->provides.d, p->provides.len, sizeof(p->provides.d[0]), char_cmp);
	if (p->requires.d)
		qsort(p->requires.d, p->requires.len, sizeof(p->requires.d[0]), char_cmp);
	DL_SORT(p->depends, pkg_dep_cmp);
	DL_SORT(p->files, pkg_file_cmp);
	DL_SORT(p->dirs, pkg_dir_cmp);
	DL_SORT(p->options, pkg_option_cmp);
	DL_SORT(p->config_files, pkg_cf_cmp);
}
