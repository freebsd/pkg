#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

#include "util.h"

struct pkg {
	char name[100];
	char version[10];
	char origin[100];
	char comment[150];
	char *desc;
	struct array deps;
	struct array rdeps;
	struct array conflicts;
	struct array files;
};

struct pkg_conflict {
	char name[100];
	char version[10];
	char origin[100];
};

struct pkg_file {
	char path[MAXPATHLEN];
	char md5[33];
};

#endif
