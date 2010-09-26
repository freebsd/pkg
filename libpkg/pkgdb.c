#include <stdio.h>
#include <stdlib.h>

#include "pkgdb.h"
#include "pkgdb_cache.h"



struct pkg **
pkgdb_list_packages(const char *pattern) {
	/* first check if the cache has to be rebuild */
	struct pkg **pkgs;

	pkgdb_cache_update();
	pkgs = pkgdb_cache_list_packages(pattern);

	return (pkgs);
}

void
pkgdb_free(struct pkg **pkgs)
{
	int i;
	struct pkg *p;

	if (pkgs) {
		for (i = 0; pkgs[i] != NULL; i++) {
			p = pkgs[i];
			if (p->name) free(p->name);
			if (p->version) free(p->version);
			if (p->comment) free(p->comment);
			if (p->desc) free(p->desc);
			if (p->origin) free(p->origin);
			free(p);
		}
		free(pkgs);
	}
}

size_t
pkgdb_count(struct pkg **pkgs)
{
	size_t i;

	if (!pkgs)
		return 0;

	for (i = 0; pkgs[i] != NULL; i++);
	return (i);
}

