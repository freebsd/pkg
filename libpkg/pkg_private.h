#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sbuf.h>

#include <archive.h>

#include "pkg_util.h"

#define PKG_NUM_FIELDS 14

struct pkg {
	struct {
		struct sbuf *value;
		unsigned int optional :1;
	} fields[PKG_NUM_FIELDS];
	int64_t flatsize;
	int64_t new_flatsize;
	int64_t new_pkgsize;
	struct array deps;
	struct array rdeps;
	struct array conflicts;
	struct array files;
	struct array scripts;
	struct array exec;
	struct array options;
	int flags;
	int64_t rowid;
	pkg_t type;
};

struct pkg_conflict {
	struct sbuf *glob;
};

struct pkg_script {
	struct sbuf *data;
	pkg_script_t type;
};

struct pkg_exec {
	struct sbuf *cmd;
	pkg_exec_t type;
};

struct pkg_file {
	char path[MAXPATHLEN];
	char sha256[65];
};

struct pkg_option {
	struct sbuf *opt;
	struct sbuf *value;
};

void pkg_conflict_free_void(void *);
void pkg_script_free_void(void *);
void pkg_exec_free_void(void *);
void pkg_option_free_void(void *);

int pkg_open2(struct pkg **p, struct archive **a, struct archive_entry **ae, const char *path);

struct packing;

int packing_init(struct packing **pack, const char *path, pkg_formats format);
int packing_append_file(struct packing *pack, const char *filepath, const char *newpath);
int packing_append_buffer(struct packing *pack, const char *buffer, const char *path, int size);
int packing_finish(struct packing *pack);

void pkg_free_void(void *);
#endif
