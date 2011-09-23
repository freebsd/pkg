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
	(*d)->try = 0;

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
 * Conflict
 */

int
pkg_conflict_new(struct pkg_conflict **c)
{
	if ((*c = calloc(1, sizeof(struct pkg_conflict))) == NULL) {
		pkg_emit_errno("calloc", "pkg_conflict");
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
