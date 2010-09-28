#ifndef _PKG_H
#define _PKG_H

#include <cdb.h>
#include <stdio.h> /* for size_t */
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

typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;
int pkg_create(char *, pkg_formats, char *, char *);
#endif
