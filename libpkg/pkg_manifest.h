#ifndef _PKG_MANIFEST_H
#define _PKG_MANIFEST_H

#include <stdbool.h>

#include <jansson.h>

enum array_type {
	ARRAY_FILES,
	ARRAY_DEPS,
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

/* Parser/Emitter */
int pkg_manifest_loadb(struct pkg_manifest *, const char *);
int pkg_manifest_loadp(struct pkg_manifest *, const char *);
char * pkg_manifest_dumpb(struct pkg_manifest *);
int pkg_manifest_dumpp(struct pkg_manifest *, const char *);
void pkg_manifest_free(struct pkg_manifest *);

/* Getter */
const char * pkg_manifest_value(struct pkg_manifest *, const char *);
bool pkg_manifest_with_option(struct pkg_manifest *, const char *);

void pkg_manifest_file_init(struct pkg_manifest *);
int pkg_manifest_file_next(struct pkg_manifest *);
const char * pkg_manifest_file_path(struct pkg_manifest *);
const char * pkg_manifest_file_md5(struct pkg_manifest *);

void pkg_manifest_dep_init(struct pkg_manifest *);
int pkg_manifest_dep_next(struct pkg_manifest *);
const char * pkg_manifest_dep_name(struct pkg_manifest *);
const char * pkg_manifest_dep_origin(struct pkg_manifest *);
const char * pkg_manifest_dep_version(struct pkg_manifest *);

void pkg_manifest_exec_init(struct pkg_manifest *);
const char * pkg_manifest_exec_next(struct pkg_manifest *);

void pkg_manifest_unexec_init(struct pkg_manifest *);
const char * pkg_manifest_unexec_next(struct pkg_manifest *);

/* Setter */
void pkg_manifest_add_value(struct pkg_manifest *, const char *, const char *);
void pkg_manifest_add_file(struct pkg_manifest *, const char *, const char *);
void pkg_manifest_add_dep(struct pkg_manifest *, const char *, const char *, const char *);
void pkg_manifest_add_exec(struct pkg_manifest *, const char *);
void pkg_manifest_add_unexec(struct pkg_manifest *, const char *);

/* Helper */
const char * pkg_manifest_keystr(json_t *, const char *);
void pkg_manifest_array_init(struct pkg_manifest *, enum array_type);
#endif
