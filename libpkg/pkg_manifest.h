#ifndef _PKG_MANIFEST_H
#define _PKG_MANIFEST_H

#include <stdbool.h>

struct pkg;

/* Opaque struct */
struct pkg_manifest;

/* Parser/Emitter */
struct pkg_manifest * pkg_manifest_new(void);
struct pkg_manifest * pkg_manifest_load_buffer(const char *);
struct pkg_manifest * pkg_manifest_load_file(const char *);
char * pkg_manifest_dump_buffer(struct pkg_manifest *);
int pkg_manifest_dump_file(struct pkg_manifest *, const char *);
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

void pkg_manifest_conflict_init(struct pkg_manifest *);
const char * pkg_manifest_conflict_next(struct pkg_manifest *);

void pkg_manifest_exec_init(struct pkg_manifest *);
const char * pkg_manifest_exec_next(struct pkg_manifest *);

void pkg_manifest_unexec_init(struct pkg_manifest *);
const char * pkg_manifest_unexec_next(struct pkg_manifest *);

/* Setter */
void pkg_manifest_add_value(struct pkg_manifest *, const char *, const char *);
void pkg_manifest_add_file(struct pkg_manifest *, const char *, const char *);
void pkg_manifest_add_dep(struct pkg_manifest *, const char *, const char *, const char *);
void pkg_manifest_add_conflict(struct pkg_manifest *, const char *);
void pkg_manifest_add_exec(struct pkg_manifest *, const char *);
void pkg_manifest_add_unexec(struct pkg_manifest *, const char *);

int pkg_manifest_from_pkg(struct pkg *, struct pkg_manifest **);

#endif
