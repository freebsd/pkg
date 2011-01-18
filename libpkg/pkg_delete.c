#include <err.h>
#include <unistd.h>

#include "pkg.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, int force)
{
	struct pkg **rdeps;
	struct pkg_file **files;

	if (pkg == NULL || db == NULL)
		return (-1);

	rdeps = pkg_rdeps(pkg);
	files = pkg_files(pkg);

	if (rdeps == NULL || files == NULL)
		return (-1);

	if (rdeps[0] != NULL && force == 0) {
		warnx("%s is required by other packages", pkg_get(pkg, PKG_ORIGIN));
		return (-1); /* TODO: special return code */
	}

	for (int i = 0; files[i] != NULL; i++) {
		if (unlink(pkg_file_path(files[i])) == -1) {
			warn("unlink(%s)", pkg_file_path(files[i]));
			continue;
		}
	}

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}
