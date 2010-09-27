#ifndef _PKG_H
#define _PKG_H

#include <cdb.h>
#include <sys/queue.h>

struct pkg {
	const char *name;
	const char *version;
	const char *origin;
	const char *comment;
	const char *desc;
	TAILQ_ENTRY(pkg) entry;
	TAILQ_HEAD(, pkg) deps;
};

struct pkgdb {
	TAILQ_HEAD(, pkg) pkgs;
	size_t count;
	struct cdb db;
};

#endif
