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

int
pkg_file_new(struct pkg_file **file)
{
	if ((*file = calloc(1, sizeof(struct pkg_file))) == NULL)
		return (EPKG_FATAL);

	(*file)->perm = 0;
	(*file)->fflags = 0;

	return (EPKG_OK);
}

void
pkg_file_free(struct pkg_file *file)
{
	free(file->sum);
	free(file);
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
	(*d)->fflags = 0;

	return (EPKG_OK);
}

void
pkg_dir_free(struct pkg_dir *d)
{
	free(d);
}

/*
 * Script
 */

const char *
pkg_script_get(struct pkg const * const p, pkg_script i)
{
	if (p->scripts[i] == NULL)
		return (NULL);

	if (sbuf_done(p->scripts[i]) == 0)
		sbuf_finish(p->scripts[i]);

	return (sbuf_data(p->scripts[i]));
}

/*
 * Option
 */

int
pkg_option_new(struct pkg_option **option)
{
	if ((*option = calloc(1, sizeof(struct pkg_option))) == NULL) {
		pkg_emit_errno("calloc", "pkg_option");
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

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

	free(c->uid);
	free(c->digest);
	free(c);
}

/*
 * Config files
 */
int
pkg_config_file_new(struct pkg_config_file **c)
{
	if ((*c = calloc(1, sizeof(struct pkg_config_file))) == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

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

int
pkg_kv_new(struct pkg_kv **c, const char *key, const char *val)
{
	if ((*c = calloc(1, sizeof(struct pkg_kv))) == NULL)
		return (EPKG_FATAL);

	(*c)->key = strdup(key);
	(*c)->value = strdup(val);

	return (EPKG_OK);
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
