#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"
#include "pkg_util.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, int force)
{
	return (pkg_delete2(pkg, db, force, 0));
}

int
pkg_delete2(struct pkg *pkg, struct pkgdb *db, int force, int upgrade)
{
	struct pkg_dep *rdep = NULL;
	int ret;

	assert(pkg != NULL);
	assert(db != NULL);

	/*
	 * Do not trust the existing entries as it may have changed if we
	 * delete packages in batch.
	 */
	pkg_list_free(pkg, PKG_RDEPS);

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

	if (!upgrade)
		pkg_emit_deinstall_begin(pkg);
	else
		pkg_emit_upgrade_begin(pkg);

	/* If there are dependencies */
	if (pkg_rdeps(pkg, &rdep) == EPKG_OK) {
		pkg_emit_required(pkg, force);
		if (!force)
			return (EPKG_REQUIRED);
	}

	if (upgrade) {
		if (( ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_UPGRADE)) != EPKG_OK )
			return (ret);
	} else {
		if ((ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_DEINSTALL)) != EPKG_OK)
			return (ret);
	}

	if ((ret = pkg_delete_files(pkg, force)) != EPKG_OK)
		return (ret);

	if (!upgrade)
		if ((ret = pkg_script_run(pkg, PKG_SCRIPT_POST_DEINSTALL)) != EPKG_OK)
			return (ret);

	if ((ret = pkg_delete_dirs(db, pkg, force)) != EPKG_OK)
		return (ret);

	if (!upgrade)
		pkg_emit_deinstall_finished(pkg);

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}

int
pkg_delete_files(struct pkg *pkg, int force)
{
	struct pkg_file *file = NULL;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	const char *path;

	while (pkg_files(pkg, &file) == EPKG_OK) {
		path = pkg_file_path(file);

		/* Regular files and links */
		/* check sha256 */
		if (!force && pkg_file_sha256(file)[0] != '\0') {
			if (sha256_file(path, sha256) == -1) {
				pkg_emit_error("sha256 calculation failed for '%s'",
					  path);
			} else if (strcmp(sha256, pkg_file_sha256(file)) != 0) {
				pkg_emit_error("%s fails original SHA256 checksum,"
							   " not removing", path);
				continue;
			}
		}

		if (unlink(path) == -1) {
			pkg_emit_errno("unlink", path);
			continue;
		}
	}

	return (EPKG_OK);
}

int
pkg_delete_dirs(struct pkgdb *db, struct pkg *pkg, int force)
{
	struct pkg_dir *dir = NULL;
	int64_t nbpackage;

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		nbpackage = 0;

		if (pkgdb_is_dir_used(db, pkg_dir_path(dir), &nbpackage) != EPKG_OK)
			return (EPKG_FATAL);

		if (nbpackage > 1)
			continue;

		if (rmdir(pkg_dir_path(dir)) == -1 && errno != ENOTEMPTY && force != 1)
			pkg_emit_errno("rmdir", pkg_dir_path(dir));
	}

	return (EPKG_OK);
}
