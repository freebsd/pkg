/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

/*
 * Dep
 */
void
pkg_dep_free(struct pkg_dep *d)
{
	if (d == NULL)
		return;

	free(d->origin);
	free(d->name);
	free(d->version);
	free(d->uid);
	free(d);
}

const char *
pkg_dep_get(struct pkg_dep const * const d, const pkg_dep_attr attr)
{
	assert(d != NULL);

	switch (attr) {
	case PKG_DEP_NAME:
		return (d->name);
		break;
	case PKG_DEP_ORIGIN:
		return (d->origin);
		break;
	case PKG_DEP_VERSION:
		return (d->version);
		break;
	default:
		return (NULL);
		break;
	}
}

bool
pkg_dep_is_locked(struct pkg_dep const * const d)
{
	assert(d != NULL);

	return d->locked;
}

/*
 * File
 */
void
pkg_file_free(struct pkg_file *file)
{

	free(file->sum);
	free(file);
}

/*
 * Script
 */

const char *
pkg_script_get(struct pkg const * const p, pkg_script i)
{
	if (p->scripts[i] == NULL)
		return (NULL);

	fflush(p->scripts[i]->fp);
	return (p->scripts[i]->buf);
}

/*
 * Option
 */
void
pkg_option_free(struct pkg_option *option)
{
	if (option == NULL)
		return;

	free(option->key);
	free(option->value);
	free(option->default_value);
	free(option->description);
	free(option);
}

/*
 * Conflicts
 */
void
pkg_conflict_free(struct pkg_conflict *c)
{
	if (c == NULL)
		return;

	free(c->uid);
	free(c->digest);
	free(c);
}

/*
 * Config files
 */
void
pkg_config_file_free(struct pkg_config_file *c)
{
	if (c == NULL)
		return;

	free(c->content);
	free(c);
}


/*
 * kv
 */

struct pkg_kv *
pkg_kv_new(const char *key, const char *val)
{
	struct pkg_kv *c;

	c = xcalloc(1, sizeof(struct pkg_kv));
	c->key = xstrdup(key);
	c->value = xstrdup(val);

	return (c);
}

void
pkg_kv_free(struct pkg_kv *c)
{
	if (c == NULL)
		return;

	free(c->key);
	free(c->value);
	free(c);
}

static int
kv_cmp(const void *a, const void *b) {
	struct pkg_kv *ka = (struct pkg_kv *)a;
	struct pkg_kv *kb = (struct pkg_kv *)b;

	return (strcmp(ka->key, kb->key));
}

struct pkg_kv *
pkg_kv_search(kvlist_t *kv, char *el)
{
	struct pkg_kv target =  { .key = el, .value = NULL };
	if (kv->len == 0)
		return (NULL);
	struct pkg_kv *res = bsearch(&target, *kv->d, kv->len, sizeof(struct pkg_kv), kv_cmp);
	if (res == NULL)
		return (NULL);
	return (res);
}

void
pkg_kv_sort(kvlist_t *kv)
{
	if (kv->len == 0)
		return;
	qsort(kv->d, kv->len, sizeof(kv->d[0]), kv_cmp);
}

struct pkg_kvlist_iterator *
pkg_kvlist_iterator(struct pkg_kvlist *l)
{
	struct pkg_kvlist_iterator *it = xcalloc(1, sizeof(struct pkg_kvlist_iterator));
	it->list = l->list;
	return (it);
};

struct pkg_kv *
pkg_kvlist_next(struct pkg_kvlist_iterator *it)
{
	if (it->pos >= it->list->len)
		return (NULL);
	return (it->list->d[it->pos++]);
}

struct pkg_stringlist_iterator *
pkg_stringlist_iterator(struct pkg_stringlist *l)
{
	struct pkg_stringlist_iterator *it = xcalloc(1, sizeof(struct pkg_stringlist_iterator));
	it->list = l->list;
	return (it);
};

const char *
pkg_stringlist_next(struct pkg_stringlist_iterator *it)
{
	if (it->pos >= it->list->len)
		return (NULL);
	return (it->list->d[it->pos++]);
}

struct pkg_el *
pkg_get_element(struct pkg *p, pkg_attr a)
{
	struct pkg_el *e = xcalloc(1, sizeof(*e));

	switch (a) {
	case PKG_ATTR_NAME:
		e->string = p->name;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_MAINTAINER:
		e->string = p->maintainer;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_PREFIX:
		e->string = p->prefix;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_DESC:
		e->string = p->desc;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_WWW:
		e->string = p->www;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_ABI:
		e->string = p->abi;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_ARCH:
		e->string = p->altabi;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_VERSION:
		e->string = p->version;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_ORIGIN:
		e->string = p->origin;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_UNIQUEID:
		e->string = p->uid;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_CKSUM:
		e->string = p->sum;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_REPONAME:
		e->string = p->reponame;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_REPOPATH:
		e->string = p->repopath;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_REPOURL:
		e->string = p->repourl;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_REASON:
		e->string = p->reason;
		e->type = PKG_STR;
		break;
	case PKG_ATTR_AUTOMATIC:
		e->boolean = p->automatic;
		e->type = PKG_BOOLEAN;
		break;
	case PKG_ATTR_LOCKED:
		e->boolean = p->locked;
		e->type = PKG_BOOLEAN;
		break;
	case PKG_ATTR_VITAL:
		e->boolean = p->vital;
		e->type = PKG_BOOLEAN;
		break;
	case PKG_ATTR_FLATSIZE:
		e->integer = p->flatsize;
		e->type = PKG_INTEGER;
		break;
	case PKG_ATTR_OLD_FLATSIZE:
		e->integer = p->old_flatsize;
		e->type = PKG_INTEGER;
		break;
	case PKG_ATTR_PKGSIZE:
		e->integer = p->pkgsize;
		e->type = PKG_INTEGER;
		break;
	case PKG_ATTR_CATEGORIES:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->categories;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_ATTR_ANNOTATIONS:
		e->kvlist = xcalloc(1, sizeof(struct pkg_kvlist *));
		e->kvlist->list = &p->annotations;
		e->type = PKG_KVLIST;
		break;
	case PKG_ATTR_SHLIBS_REQUIRED:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->shlibs_required;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_ATTR_SHLIBS_PROVIDED:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->shlibs_provided;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_ATTR_PROVIDES:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->provides;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_ATTR_REQUIRES:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->requires;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_ATTR_USERS:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->users;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_ATTR_GROUPS:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->groups;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_ATTR_LICENSES:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->licenses;
		e->type = PKG_STRINGLIST;
		break;
	}

	return (e);
}
