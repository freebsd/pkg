#include <stdlib.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

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
pkg_dep_origin(struct pkg_dep *d)
{
	return (sbuf_get(d->origin));
}

const char *
pkg_dep_name(struct pkg_dep *d)
{
	return (sbuf_get(d->name));
}

const char *
pkg_dep_version(struct pkg_dep *d)
{
	return (sbuf_get(d->version));
}

/*
 * File
 */

int
pkg_file_new(struct pkg_file **file)
{
	if ((*file = calloc(1, sizeof(struct pkg_file))) == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

void
pkg_file_free(struct pkg_file *file)
{
	free(file);
}

const char *
pkg_file_path(struct pkg_file *file)
{
	return (file->path);
}

const char *
pkg_file_sha256(struct pkg_file *file)
{
	return (file->sha256);
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
	return (c->name);
}

void
pkg_category_free(struct pkg_category *c)
{
	free(c);
}

/*
 * Conflict
 */

int
pkg_conflict_new(struct pkg_conflict **c)
{
	if ((*c = calloc(1, sizeof(struct pkg_conflict))) == NULL) {
		EMIT_ERRNO("calloc", "pkg_conflict");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

void
pkg_conflict_free(struct pkg_conflict *c)
{
	if (c == NULL)
		return;

	sbuf_free(c->glob);
	free(c);
}

const char *
pkg_conflict_glob(struct pkg_conflict *c)
{
	return (sbuf_get(c->glob));
}

/*
 * Script
 */

int
pkg_script_new(struct pkg_script **script)
{
	if ((*script = calloc(1, sizeof(struct pkg_script))) == NULL) {
		EMIT_ERRNO("calloc", "pkg_script");
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
	if ((*option = calloc(1, sizeof(struct pkg_option))))
		return (-1);
	return (0);
}

void
pkg_option_free(struct pkg_option *option)
{
	sbuf_free(option->key);
	sbuf_free(option->value);
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
