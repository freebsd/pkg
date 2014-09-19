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
int
pkg_dep_new(struct pkg_dep **d)
{
	if ((*d = calloc(1, sizeof(struct pkg_dep))) == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

void
pkg_dep_free(struct pkg_dep *d)
{
	if (d == NULL)
		return;

	sbuf_free(d->origin);
	sbuf_free(d->name);
	sbuf_free(d->version);
	free(d->uid);
	free(d);
}

const char *
pkg_dep_get(struct pkg_dep const * const d, const pkg_dep_attr attr)
{
	assert(d != NULL);

	switch (attr) {
	case PKG_DEP_NAME:
		return (sbuf_get(d->name));
		break;
	case PKG_DEP_ORIGIN:
		return (sbuf_get(d->origin));
		break;
	case PKG_DEP_VERSION:
		return (sbuf_get(d->version));
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

int
pkg_file_new(struct pkg_file **file)
{
	if ((*file = calloc(1, sizeof(struct pkg_file))) == NULL)
		return (EPKG_FATAL);

	(*file)->perm = 0;
	(*file)->keep = 0;

	return (EPKG_OK);
}

void
pkg_file_free(struct pkg_file *file)
{
	free(file);
}

const char *
pkg_file_get(struct pkg_file const * const f, const pkg_file_attr attr)
{
	assert(f != NULL);

	switch (attr) {
	case PKG_FILE_PATH:
		return (f->path);
		break;
	case PKG_FILE_SUM:
		return (f->sum);
		break;
	case PKG_FILE_UNAME:
		return (f->uname);
		break;
	case PKG_FILE_GNAME:
		return (f->gname);
		break;
	default:
		return (NULL);
		break;
	}
}

mode_t
pkg_file_mode(struct pkg_file const * const f)
{
	assert(f != NULL);

	return (f->perm);
}

bool
pkg_file_keep(struct pkg_file const * const f)
{
	assert(f != NULL);

	return (f->keep);
}

/*
 * Dir
 */

int
pkg_dir_new(struct pkg_dir **d)
{
	if ((*d = calloc(1, sizeof(struct pkg_dir))) == NULL)
		return (EPKG_FATAL);

	(*d)->perm = 0;
	(*d)->keep = 0;
	(*d)->try = false;

	return (EPKG_OK);
}

void
pkg_dir_free(struct pkg_dir *d)
{
	free(d);
}

const char *
pkg_dir_get(struct pkg_dir const * const d, const pkg_dir_attr attr)
{
	assert(d != NULL);
	switch (attr) {
	case PKG_DIR_PATH:
		return (d->path);
		break;
	case PKG_DIR_UNAME:
		return (d->uname);
		break;
	case PKG_DIR_GNAME:
		return (d->gname);
		break;
	default:
		return(NULL);
	}
}

mode_t
pkg_dir_mode(struct pkg_dir const * const d)
{
	assert(d != NULL);

	return (d->perm);
}

bool
pkg_dir_keep(struct pkg_dir const * const d)
{
	assert(d != NULL);

	return (d->keep);
}

bool
pkg_dir_try(struct pkg_dir const * const d)
{
	assert(d != NULL);

	return (d->try);
}

/*
 * User
 */

int
pkg_user_new(struct pkg_user **u)
{
	if ((*u = calloc(1, sizeof(struct pkg_user))) == NULL) {
		pkg_emit_errno("calloc", "pkg_user");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

void
pkg_user_free(struct pkg_user *u)
{
	free(u);
}

const char *
pkg_user_name(struct pkg_user const * const u)
{
	assert(u != NULL);

	return (u->name);
}

const char *
pkg_user_uidstr(struct pkg_user const * const u)
{
	assert(u != NULL);

	return (u->uidstr);
}

/*
 * Group
 */

int
pkg_group_new(struct pkg_group **g)
{
	if ((*g = calloc(1, sizeof(struct pkg_group))) == NULL) {
		pkg_emit_errno("calloc", "pkg_group");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

void
pkg_group_free(struct pkg_group *g)
{
	free(g);
}

const char *
pkg_group_name(struct pkg_group const * const g)
{
	assert(g != NULL);

	return (g->name);
}

const char *
pkg_group_gidstr(struct pkg_group const * const g)
{
	assert(g != NULL);

	return (g->gidstr);
}

/*
 * Script
 */

const char *
pkg_script_get(struct pkg const * const p, pkg_script i)
{
	if (p->scripts[i] == NULL)
		return (NULL);

	return (sbuf_get(p->scripts[i]));
}

/*
 * Option
 */

int
pkg_option_new(struct pkg_option **option)
{
	if ((*option = calloc(1, sizeof(struct pkg_option))) == NULL) {
		pkg_emit_errno("calloc", "pkg_user");
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

void
pkg_option_free(struct pkg_option *option)
{
	if (option == NULL)
		return;

	sbuf_free(option->key);
	sbuf_free(option->value);
	sbuf_free(option->default_value);
	sbuf_free(option->description);
	free(option);
}

const char *
pkg_option_opt(struct pkg_option const * const option)
{
	assert(option != NULL);

	return (sbuf_get(option->key));
}

const char *
pkg_option_value(struct pkg_option const * const option)
{
	assert(option != NULL);

	return (sbuf_get(option->value));
}

const char *
pkg_option_default_value(struct pkg_option const * const option)
{
	assert(option != NULL);

	return (sbuf_get(option->default_value));
}

const char *
pkg_option_description(struct pkg_option const * const option)
{
	assert(option != NULL);

	return (sbuf_get(option->description));
}

/*
 * Shared Libraries
 */
int
pkg_shlib_new(struct pkg_shlib **sl)
{
	if ((*sl = calloc(1, sizeof(struct pkg_shlib))) == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

void
pkg_shlib_free(struct pkg_shlib *sl)
{
	if (sl == NULL)
		return;

	sbuf_free(sl->name);
	free(sl);
}

const char *
pkg_shlib_name(struct pkg_shlib const * const sl)
{
	assert(sl != NULL);

	return (sbuf_get(sl->name));
}

/*
 * Conflicts
 */

int
pkg_conflict_new(struct pkg_conflict **c)
{
	if ((*c = calloc(1, sizeof(struct pkg_conflict))) == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

void
pkg_conflict_free(struct pkg_conflict *c)
{
	if (c == NULL)
		return;

	sbuf_free(c->uniqueid);
	free(c);
}

const char *
pkg_conflict_uniqueid(const struct pkg_conflict *c)
{
	assert(c != NULL);

	return (sbuf_get(c->uniqueid));
}

/*
 * Provides
 */
int
pkg_provide_new(struct pkg_provide **c)
{
	if ((*c = calloc(1, sizeof(struct pkg_provide))) == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

void
pkg_provide_free(struct pkg_provide *c)
{
	if (c == NULL)
		return;

	sbuf_free(c->provide);
	free(c);
}

const char *
pkg_provide_name(const struct pkg_provide *c)
{
	assert(c != NULL);

	return (sbuf_get(c->provide));
}
