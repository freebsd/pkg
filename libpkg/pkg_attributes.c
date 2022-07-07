/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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
	if (it->cur == NULL)
		it->cur = it->list->head;
	else
		it->cur = ((__typeof__(it->list->head))it->cur)->next;
	if (it->cur == NULL)
		return (NULL);
	return (((__typeof__(it->list->head))it->cur)->item);
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
	if (it->cur == NULL)
		it->cur = it->list->head;
	else
		it->cur = ((__typeof__(it->list->head))it->cur)->next;
	if (it->cur == NULL)
		return (NULL);
	return (((__typeof__(it->list->head))it->cur)->item);
}

struct pkg_el *
pkg_get_element(struct pkg *p, pkg_attr a)
{
	struct pkg_el *e = xcalloc(1, sizeof(*e));

	switch (a) {
	case PKG_NAME:
		e->string = p->name;
		e->type = PKG_STR;
		break;
	case PKG_VERSION:
		e->string = p->version;
		e->type = PKG_STR;
		break;
	case PKG_ORIGIN:
		e->string = p->origin;
		e->type = PKG_STR;
		break;
	case PKG_UNIQUEID:
		e->string = p->uid;
		e->type = PKG_STR;
		break;
	case PKG_CKSUM:
		e->string = p->sum;
		e->type = PKG_STR;
		break;
	case PKG_REPONAME:
		e->string = p->reponame;
		e->type = PKG_STR;
		break;
	case PKG_REPOPATH:
		e->string = p->repopath;
		e->type = PKG_STR;
		break;
	case PKG_REPOURL:
		e->string = p->repourl;
		e->type = PKG_STR;
		break;
	case PKG_REASON:
		e->string = p->reason;
		e->type = PKG_STR;
		break;
	case PKG_AUTOMATIC:
		e->boolean = p->automatic;
		e->type = PKG_BOOLEAN;
		break;
	case PKG_LOCKED:
		e->boolean = p->locked;
		e->type = PKG_BOOLEAN;
		break;
	case PKG_VITAL:
		e->boolean = p->vital;
		e->type = PKG_BOOLEAN;
		break;
	case PKG_FLATSIZE:
		e->integer = p->flatsize;
		e->type = PKG_INTEGER;
		break;
	case PKG_OLD_FLATSIZE:
		e->integer = p->old_flatsize;
		e->type = PKG_INTEGER;
		break;
	case PKG_PKGSIZE:
		e->integer = p->pkgsize;
		e->type = PKG_INTEGER;
		break;
	case PKG_CATEGORIES:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->categories;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_ANNOTATIONS:
		e->kvlist = xcalloc(1, sizeof(struct pkg_kvlist *));
		e->kvlist->list = &p->annotations;
		e->type = PKG_KVLIST;
		break;
	case PKG_SHLIBS_REQUIRED:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->shlibs_required;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_SHLIBS_PROVIDED:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->shlibs_provided;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_PROVIDES:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->provides;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_REQUIRES:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->requires;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_USERS:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->users;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_GROUPS:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->groups;
		e->type = PKG_STRINGLIST;
		break;
	case PKG_LICENSES:
		e->stringlist = xcalloc(1, sizeof(struct pkg_stringlist *));
		e->stringlist->list = &p->licenses;
		e->type = PKG_STRINGLIST;
		break;
	}

	return (e);
}

bool
stringlist_contains(stringlist_t *l, const char *name)
{
	tll_foreach(*l, e) {
		if (strcmp(e->item, name) == 0)
			return (true);
	}
	return (false);
}
