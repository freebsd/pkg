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

#include <assert.h>
#include <stdlib.h>

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
	sbuf_free(d->origin);
	sbuf_free(d->name);
	sbuf_free(d->version);
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
pkg_dir_path(struct pkg_dir *d)
{
	return (d->path);
}

bool
pkg_dir_try(struct pkg_dir *d)
{
	return (d->try);
}

int
pkg_category_new(struct pkg_category **c)
{
	if (( *c = calloc(1, sizeof(struct pkg_category))) == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

const char *
pkg_category_name(struct pkg_category *c)
{
	return (sbuf_get(c->name));
}

void
pkg_category_free(struct pkg_category *c)
{

	if (c == NULL)
		return;

	sbuf_free(c->name);
	free(c);
}

/*
 * License
 */
int
pkg_license_new(struct pkg_license **l)
{
	if ((*l = calloc(1, sizeof(struct pkg_license))) == NULL) {
		pkg_emit_errno("calloc", "pkg_license");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

void
pkg_license_free(struct pkg_license *l)
{
	if (l == NULL)
		return;

	sbuf_free(l->name);
	free(l);
}

const char *
pkg_license_name(struct pkg_license *l)
{
	return (sbuf_get(l->name));
}

/*
 * user
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
	if (u == NULL)
		return;

	free(u);
}

const char *
pkg_user_name(struct pkg_user *u)
{
	return (u->name);
}

const char *
pkg_user_uidstr(struct pkg_user *u)
{
	return (u->uidstr);
}

/*
 * group
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
	if (g == NULL)
		return;

	free(g);
}

const char *
pkg_group_name(struct pkg_group *g)
{
	return (g->name);
}

const char *
pkg_group_gidstr(struct pkg_group *g)
{
	return (g->gidstr);
}


/*
 * Script
 */

int
pkg_script_new(struct pkg_script **script)
{
	if ((*script = calloc(1, sizeof(struct pkg_script))) == NULL) {
		pkg_emit_errno("calloc", "pkg_script");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

void
pkg_script_free(struct pkg_script *script)
{
	if (script == NULL)
		return;

	sbuf_free(script->data);
	free(script);
}

const char *
pkg_script_data(struct pkg_script *s)
{
	return (sbuf_get(s->data));
}

pkg_script_t
pkg_script_type(struct pkg_script *s)
{
	return (s->type);
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
	free(option);
}

const char *
pkg_option_opt(struct pkg_option *option)
{
	return (sbuf_get(option->key));
}

const char *
pkg_option_value(struct pkg_option *option)
{
	return (sbuf_get(option->value));
}

/*
 * Shared Libraries
 */
int
pkg_shlib_new(struct pkg_shlib **sl)
{
	if (( *sl = calloc(1, sizeof(struct pkg_shlib))) == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

const char *
pkg_shlib_name(struct pkg_shlib *sl)
{
	return (sbuf_get(sl->name));
}

void
pkg_shlib_free(struct pkg_shlib *sl)
{

	if (sl == NULL)
		return;

	sbuf_free(sl->name);
	free(sl);
}
