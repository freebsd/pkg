#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

#include <sys/types.h>
#include <sys/sbuf.h>

#include "util.h"

typedef enum {
	PKG_FILE,
	PKG_REMOTE,
	PKG_INSTALLED,
	PKG_NOTFOUND
} pkg_t;
struct pkg {
	struct sbuf *origin;
	struct sbuf *name;
	struct sbuf *version;
	struct sbuf *comment;
	struct sbuf *desc;
	struct array deps;
	struct array rdeps;
	struct array conflicts;
	struct array files;
	pkg_t type;
};

struct pkg_conflict {
	struct sbuf *glob;
};

struct pkg_file {
	char path[MAXPATHLEN];
	char md5[33];
};

#endif
