#include <string.h>
#include <err.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"
#include "pkg_util.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, int force)
{
	struct pkg_dep *rdep = NULL;
	int ret;
	struct sbuf *rdep_msg = NULL;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (db == NULL)
		return (ERROR_BAD_ARG("db"));

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

	while (pkg_rdeps(pkg, &rdep) == EPKG_OK) {
		if (rdep_msg == NULL) {
			rdep_msg = sbuf_new_auto();
			sbuf_printf(rdep_msg, "%s-%s is required by other packages:", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		}
		sbuf_cat(rdep_msg, " ");
		sbuf_printf(rdep_msg, "%s-%s", pkg_dep_name(rdep), pkg_dep_version(rdep));
	}
	/* If there are dependencies */
	if (rdep_msg != NULL) {
		if (!force) {
			sbuf_finish(rdep_msg);
			pkg_emit_event(PKG_EVENT_DELETE_DEP_EXISTS, /*argc*/1,
			    sbuf_get(rdep_msg));
			ret = EPKG_REQUIRED;
			sbuf_free(rdep_msg);
			return ret;
		}
		sbuf_cat(rdep_msg, ", deleting anyway");
		sbuf_finish(rdep_msg);
		fprintf(stderr, "%s\n", sbuf_get(rdep_msg));
		sbuf_free(rdep_msg);
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
				warnx("sha256 calculation failed for '%s'",
					  path);
			} else if (strcmp(sha256, pkg_file_sha256(file)) != 0) {
				warnx("%s fails original SHA256 checksum, not removing", path);
				continue;
			}
		}

		if (unlink(path) == -1) {
			warn("unlink(%s)", path);
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
			warn("rmdir(%s)", pkg_dir_path(dir));
		}
	}

	return (EPKG_OK);
}
