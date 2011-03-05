#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sbuf.h>

#include "pkg_util.h"

struct pkg {
	struct sbuf *origin;
	struct sbuf *name;
	struct sbuf *version;
	struct sbuf *comment;
	struct sbuf *desc;
	struct sbuf *mtree;
	struct sbuf *message;
	struct sbuf *arch;
	struct sbuf *osversion;
	struct sbuf *maintainer;
	struct sbuf *www;
	struct sbuf *err;
	struct sbuf *prefix;
	int64_t flatsize;
	struct array deps;
	struct array rdeps;
	struct array conflicts;
	struct array files;
	struct array scripts;
	struct array exec;
	struct array options;
	const char *path;
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

#endif
