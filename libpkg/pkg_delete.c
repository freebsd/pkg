#include <string.h>
#include <err.h>
#include <unistd.h>
#include <sha256.h>

#include "pkg.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, int force)
{
	struct pkg **rdeps;
	struct pkg_file **files;
	char sha256[65];

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
		/* check sha256 */
		if (pkg_file_sha256(files[i])[0] != '\0' &&
			(SHA256_File(pkg_file_path(files[i]), sha256) == NULL ||
			strcmp(sha256, pkg_file_sha256(files[i])) != 0))
			warnx("%s fails original SHA256 checksum, not removed",
					pkg_file_path(files[i]));

		else if (unlink(pkg_file_path(files[i])) == -1) {
			warn("unlink(%s)", pkg_file_path(files[i]));
			continue;
		}
	}

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}
