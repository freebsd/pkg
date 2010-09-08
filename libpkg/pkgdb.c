#include "pkgdb.h"
#include "pkgdb_cache.h"

struct pkg **
pkgdb_list_packages() {
	/* first check if the cache has to be rebuild */
	struct pkg **pkgs;
	pkgdb_cache_update();
	pkgs = pkgdb_cache_list_packages();
	
	return (pkgs);
}
