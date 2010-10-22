#include <assert.h>
#include <err.h>
#include <stdbool.h>
#include <stdlib.h>

#include <jansson.h>

#include "pkg.h"
#include "pkg_manifest.h"

enum array_type {
	ARRAY_FILES,
	ARRAY_DEPS,
	ARRAY_CONFLICTS,
	ARRAY_EXEC,
	ARRAY_UNEXEC
};

struct pkg_manifest_array {
	json_t *node;
	json_t *elm;
	unsigned int size;
	unsigned int idx;
	enum array_type type;
};

struct pkg_manifest {
	json_t *json;
	struct pkg_manifest_array array;
};

/* Helpers */
const char * pkg_manifest_keystr(json_t *, const char *);
void pkg_manifest_array_init(struct pkg_manifest *, enum array_type);
const char * pkg_manifest_simplenext(struct pkg_manifest *, enum array_type,
									 const char *);

const char *
pkg_manifest_keystr(json_t *root, const char *key)
{
	json_t *node;

	if ((node = json_object_get(root, key)) == NULL)
		return (NULL);
	else
		return json_string_value(node);
}

void
pkg_manifest_array_init(struct pkg_manifest *m, enum array_type t)
{
	m->array.node = NULL;
	m->array.elm = NULL;
	m->array.size = 0;
	m->array.idx = 0;
	m->array.type = t;
}

const char *
pkg_manifest_simplenext(struct pkg_manifest *m, enum array_type t, const char *key)
{
	assert(m->array.type == t);

	/* First call */
	if (m->array.node == NULL) {
		if ((m->array.node = json_object_get(m->json, key)) == NULL)
			return (NULL);
		m->array.size = json_array_size(m->array.node);
	}

	if (m->array.idx >= m->array.size)
		return (NULL);

	if ((m->array.elm = json_array_get(m->array.node, m->array.idx)) == NULL)
		return (NULL);

	m->array.idx++;

	return json_string_value(m->array.elm);
}

/*
 * Simple accesser for string value at the root of the JSON node
 */
const char *
pkg_manifest_value(struct pkg_manifest *manifest, const char *key)
{
	const char *str;

	if ((str = pkg_manifest_keystr(manifest->json, key)) == NULL)
		warnx("Can not find '%s' in the manifest", key);

	return (str);
}

/*
 * Return true if the package was built with option named `name'
 */
bool
pkg_manifest_with_option(struct pkg_manifest *manifest, const char *name)
{
	json_t *node;
	json_t *elm;

	if ((node = json_object_get(manifest->json, "options")) == NULL)
		return (false);

	if ((elm = json_object_get(node, name)) == NULL)
		return (false);

	return json_equal(elm, json_true());
}

void
pkg_manifest_file_init(struct pkg_manifest *m)
{
	pkg_manifest_array_init(m, ARRAY_FILES);
}

/*
 * Return 0 if there is another file, 1 if we are done, -1 if an error occured.
 */
int
pkg_manifest_file_next(struct pkg_manifest *m)
{
	assert(m->array.type == ARRAY_FILES);

	/* First call */
	if (m->array.node == NULL) {
		if ((m->array.node = json_object_get(m->json, "files")) == NULL)
			return (-1);
		m->array.size = json_array_size(m->array.node);
	}

	if (m->array.idx >= m->array.size)
		return (1);

	if ((m->array.elm = json_array_get(m->array.node, m->array.idx)) == NULL)
		return (-1);

	m->array.idx++;

	return (0);
}

const char *
pkg_manifest_file_path(struct pkg_manifest *m)
{
	assert(m->array.type == ARRAY_FILES);
	assert(m->array.elm != NULL);

	return pkg_manifest_keystr(m->array.elm, "path");
}

const char *
pkg_manifest_file_md5(struct pkg_manifest *m)
{
	assert(m->array.type == ARRAY_FILES);
	assert(m->array.elm != NULL);

	return pkg_manifest_keystr(m->array.elm, "md5");
}

void
pkg_manifest_dep_init(struct pkg_manifest *m)
{
	pkg_manifest_array_init(m, ARRAY_DEPS);
}

int
pkg_manifest_dep_next(struct pkg_manifest *m)
{
	assert(m->array.type == ARRAY_DEPS);

	/* First call */
	if (m->array.node == NULL) {
		if ((m->array.node = json_object_get(m->json, "deps")) == NULL)
			return (-1);
		m->array.size = json_array_size(m->array.node);
	}

	if (m->array.idx >= m->array.size)
		return (1);

	if ((m->array.elm = json_array_get(m->array.node, m->array.idx)) == NULL)
		return (-1);

	m->array.idx++;

	return (0);

}

const char *
pkg_manifest_dep_name(struct pkg_manifest *m)
{
	assert(m->array.type == ARRAY_DEPS);
	assert(m->array.elm != NULL);

	return pkg_manifest_keystr(m->array.elm, "name");
}

const char *
pkg_manifest_dep_origin(struct pkg_manifest *m)
{
	assert(m->array.type == ARRAY_DEPS);
	assert(m->array.elm != NULL);

	return pkg_manifest_keystr(m->array.elm, "origin");
}

const char *
pkg_manifest_dep_version(struct pkg_manifest *m)
{
	assert(m->array.type == ARRAY_DEPS);
	assert(m->array.elm != NULL);

	return pkg_manifest_keystr(m->array.elm, "version");
}

void
pkg_manifest_conflict_init(struct pkg_manifest *m)
{
	pkg_manifest_array_init(m, ARRAY_CONFLICTS);
}

const char *
pkg_manifest_conflict_next(struct pkg_manifest *m)
{
	return pkg_manifest_simplenext(m, ARRAY_CONFLICTS, "conflicts");
}

void
pkg_manifest_exec_init(struct pkg_manifest *m)
{
	pkg_manifest_array_init(m, ARRAY_EXEC);
}

const char *
pkg_manifest_exec_next(struct pkg_manifest *m)
{
	return pkg_manifest_simplenext(m, ARRAY_EXEC, "exec");
}

void
pkg_manifest_unexec_init(struct pkg_manifest *m)
{
	pkg_manifest_array_init(m, ARRAY_UNEXEC);
}

const char *
pkg_manifest_unexec_next(struct pkg_manifest *m)
{
	return pkg_manifest_simplenext(m, ARRAY_UNEXEC, "unexec");
}

/* Setters */

void
pkg_manifest_add_value(struct pkg_manifest *m, const char *key, const char *value)
{
	json_object_set_new(m->json, key, json_string(value));
}

void
pkg_manifest_add_file(struct pkg_manifest *m, const char *path, const char *md5)
{
	json_t *files;
	json_t *file;

	if ((files = json_object_get(m->json, "files")) == NULL) {
		files = json_array();
		json_object_set_new(m->json, "files", files);
	}

	file = json_object();
	json_object_set_new(file, "path", json_string(path));
	json_object_set_new(file, "md5", json_string(md5));

	json_array_append_new(files, file);
}

void
pkg_manifest_add_dep(struct pkg_manifest *m, const char *name, const char *origin,
					 const char *version)
{
	json_t *deps;
	json_t *dep;

	if ((deps = json_object_get(m->json, "deps")) == NULL) {
		deps = json_array();
		json_object_set_new(m->json, "deps", deps);
	}

	dep = json_object();
	json_object_set_new(dep, "name", json_string(name));
	json_object_set_new(dep, "origin", json_string(origin));
	json_object_set_new(dep, "version", json_string(version));

	json_array_append_new(deps, dep);
}

void
pkg_manifest_add_conflict(struct pkg_manifest *m, const char *value)
{
	json_t *array;

	if ((array = json_object_get(m->json, "conflicts")) == NULL) {
		array = json_array();
		json_object_set_new(m->json, "conflicts", array);
	}

	json_array_append_new(array, json_string(value));
}

void
pkg_manifest_add_exec(struct pkg_manifest *m, const char *value)
{
	json_t *array;

	if ((array = json_object_get(m->json, "exec")) == NULL) {
		array = json_array();
		json_object_set_new(m->json, "exec", array);
	}

	json_array_append_new(array, json_string(value));
}

void
pkg_manifest_add_unexec(struct pkg_manifest *m, const char *value)
{
	json_t *array;

	if ((array = json_object_get(m->json, "unexec")) == NULL) {
		array = json_array();
		json_object_set_new(m->json, "unexec", array);
	}

	json_array_append_new(array, json_string(value));
}

struct pkg_manifest *
pkg_manifest_new(void)
{
	struct pkg_manifest *m;

	if ((m = malloc(sizeof(struct pkg_manifest))) == NULL)
		err(EXIT_FAILURE, "malloc()");

	m->json = json_object();

	return (m);
}

struct pkg_manifest *
pkg_manifest_load_buffer(const char *buffer)
{
	struct pkg_manifest *m;
	json_error_t error;

	if ((m = malloc(sizeof(struct pkg_manifest))) == NULL)
		err(EXIT_FAILURE, "malloc()");

	if ((m->json = json_loads(buffer, &error)) == NULL) {
		warnx("Can not parse buffer as JSON: %s", error.text);
		free(m);
		return (NULL);
	}

	return (m);
}

struct pkg_manifest *
pkg_manifest_load_file(const char *path)
{
	struct pkg_manifest *m;
	json_error_t error;

	if ((m = malloc(sizeof(struct pkg_manifest))) == NULL)
		err(EXIT_FAILURE, "malloc()");

	if ((m->json = json_load_file(path, &error)) == NULL) {
		warnx("Can not parse %s: %s", path, error.text);
		free(m);
		return (NULL);
	}

	return (m);
}

int
pkg_manifest_from_pkg(struct pkg *pkg, struct pkg_manifest **m)
{
	/* TODO */
	(void)pkg;
	(void)m;
	return (0);
}

char *
pkg_manifest_dump_buffer(struct pkg_manifest *m)
{
	return json_dumps(m->json, JSON_COMPACT);
}

int
pkg_manifest_dump_file(struct pkg_manifest *m, const char *path)
{
	return json_dump_file(m->json, path, JSON_INDENT(2));
}

void
pkg_manifest_free(struct pkg_manifest *m)
{
	json_decref(m->json);
	free(m);
}

