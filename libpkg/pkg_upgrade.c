#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

int
pkg_upgrade(struct pkgdb *db, struct pkg *pkg, const char *path)
{
	int ret;

	if ((ret = pkg_delete2(pkg, db, 1, 0)) != EPKG_OK)
		return (ret);
	if ((ret = pkg_add2(db, path, 0)) != EPKG_OK)
		return (ret);

	return (EPKG_OK);
}
