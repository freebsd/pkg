#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

int
pkg_upgrade(struct pkgdb *db, struct pkg *pkg, const char *path)
{
	int ret;

	if (pkg != NULL) {
		if ((ret = pkg_delete2(pkg, db, 1, 0)) != EPKG_OK)
			return (ret);
		if ((ret = pkg_add2(db, path, 0,pkg_isautomatic(pkg))) != EPKG_OK)
			return (ret);
	} else {
		/*
		 * In upgrade case a new package is a dep so it is an automatic
		 * installation
		 */
		if ((ret = pkg_add2(db, path, 0, 1)) != EPKG_OK)
			return (ret);
	}

	return (EPKG_OK);
}
