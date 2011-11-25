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
pkg_delete(struct pkg *pkg, struct pkgdb *db, int flags)
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
	if ((ret = pkgdb_load_rdeps(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_load_files(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_load_dirs(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_load_scripts(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_load_mtree(db, pkg)) != EPKG_OK)
		return (ret);

	if (flags & PKG_DELETE_UPGRADE)
		pkg_emit_upgrade_begin(pkg);
	else
		pkg_emit_deinstall_begin(pkg);

	/* If there are dependencies */
	if (pkg_rdeps(pkg, &rdep) == EPKG_OK) {
		pkg_emit_required(pkg, flags & PKG_DELETE_FORCE);
		if (flags ^ PKG_DELETE_FORCE)
			return (EPKG_REQUIRED);
	}

	if (flags & PKG_DELETE_UPGRADE) {
		if (( ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_UPGRADE)) != EPKG_OK )
			return (ret);
	} else {
		if ((ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_DEINSTALL)) != EPKG_OK)
			return (ret);
	}

	if ((ret = pkg_delete_files(pkg, flags & PKG_DELETE_FORCE)) != EPKG_OK)
		return (ret);

	if (flags ^ PKG_DELETE_UPGRADE)
		if ((ret = pkg_script_run(pkg, PKG_SCRIPT_POST_DEINSTALL)) != EPKG_OK)
			return (ret);

	if ((ret = pkg_delete_dirs(db, pkg, flags & PKG_DELETE_FORCE)) != EPKG_OK)
		return (ret);

	if (flags ^ PKG_DELETE_UPGRADE)
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
		if (file->keep == 1)
			continue;

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
		if (dir->keep == 1)
			continue;

		nbpackage = 0;

		if (pkgdb_is_dir_used(db, pkg_dir_path(dir), &nbpackage) != EPKG_OK)
			return (EPKG_FATAL);

		if (nbpackage > 1)
			continue;

		if (pkg_dir_try(dir)) {
			if (rmdir(pkg_dir_path(dir)) == -1 && errno != ENOTEMPTY && force != 1)
				pkg_emit_errno("rmdir", pkg_dir_path(dir));
		} else {
			if (rmdir(pkg_dir_path(dir)) == -1 && force != 1)
				pkg_emit_errno("rmdir", pkg_dir_path(dir));
		}
	}

	return (EPKG_OK);
}
