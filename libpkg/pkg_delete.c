#include <string.h>
#include <err.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_error.h"
#include "pkg_private.h"
#include "pkg_util.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, int force)
{
	struct pkg_dep *rdep = NULL;
	int ret;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (db == NULL)
		return (ERROR_BAD_ARG("db"));

	/*
	 * Do not trust the existing entries as it may have changed if we
	 * delete packages in batch.
	 */
	pkg_freerdeps(pkg);

	/*
	 * Ensure that we have all the informations we need
	 */
	if ((ret = pkgdb_loadrdeps(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadfiles(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loaddirs(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadscripts(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadmtree(db, pkg)) != EPKG_OK)
		return (ret);

	/* If there are dependencies */
	if (pkg_rdeps(pkg, &rdep) == EPKG_OK) {
		EMIT_REQUIRED(pkg, force);
		if (!force)
			return (EPKG_REQUIRED);
	}

	if ((ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_DEINSTALL)) != EPKG_OK)
		return (ret);

	if ((ret = pkg_delete_files(pkg, force)) != EPKG_OK)
		return (ret);

	if ((ret = pkg_script_run(pkg, PKG_SCRIPT_POST_DEINSTALL)) != EPKG_OK)
		return (ret);

	if ((ret = pkg_delete_dirs(pkg, force)) != EPKG_OK)
		return (ret);

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}

int
pkg_delete_files(struct pkg *pkg, int force)
{
	struct pkg_file *file = NULL;
	char sha256[65];
	const char *path;

	while (pkg_files(pkg, &file) == EPKG_OK) {
		path = pkg_file_path(file);

		/* Regular files and links */
		/* check sha256 */
		if (!force && pkg_file_sha256(file)[0] != '\0') {
			if (sha256_file(path, sha256) == -1) {
				EMIT_PKG_ERROR("sha256 calculation failed for '%s'",
					  path);
			} else if (strcmp(sha256, pkg_file_sha256(file)) != 0) {
				EMIT_PKG_ERROR("%s fails original SHA256 checksum, not removing", path);
				continue;
			}
		}

		if (unlink(path) == -1) {
			EMIT_ERRNO("unlink", path);
			continue;
		}
	}

	return (EPKG_OK);
}

int
pkg_delete_dirs(struct pkg *pkg, int force)
{
	struct pkg_dir *dir = NULL;

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (rmdir(pkg_dir_path(dir)) == -1 && errno != ENOTEMPTY && force != 1) {
			EMIT_ERRNO("rmdir", pkg_dir_path(dir));
		}
	}

	return (EPKG_OK);
}
