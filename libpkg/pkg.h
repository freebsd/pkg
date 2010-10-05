#ifndef _PKG_H
#define _PKG_H

#include <cdb.h>
#include <stdio.h> /* for size_t */
#include <regex.h> /* regex_t */

struct pkg {
	const char *namever;
	const char *name;
	const char *version;
	const char *origin;
	const char *comment;
	const char *desc;
	size_t idx; /* index on pkgdb */
	size_t idep; /* iterator deps */
	size_t irdep; /* iterator rdeps */
	struct cdb *db;
	void *manifest; /* temp for pkgdb_cache */
};

typedef enum _match_t {
	MATCH_ALL,
	MATCH_EXACT,
	MATCH_GLOB,
	MATCH_REGEX,
	MATCH_EREGEX
} match_t;


struct pkgdb {
	struct cdb db;
	int lock_fd;
	size_t i; /* iterator */
	const char *pattern;
	match_t match;
	regex_t re;
};

typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;
int pkg_create(char *, pkg_formats, const char *, const char *);
#endif
