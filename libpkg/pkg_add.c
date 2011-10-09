#include <sys/utsname.h>

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

static int
dep_installed(struct pkg_dep *dep, struct pkgdb *db) {
	struct pkg *p = NULL;
	struct pkgdb_it *it;
	int ret;

	it = pkgdb_query(db, pkg_dep_origin(dep), MATCH_EXACT);

	if (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
		ret = EPKG_OK;
	} else {
		ret = EPKG_FATAL;
	}

	pkgdb_it_free(it);
	pkg_free(p);

	return (ret);
}

static int
do_extract(struct archive *a, struct archive_entry *ae)
{
	int retcode = EPKG_OK;
	int ret = 0;
	char path[MAXPATHLEN + 1];
	struct stat st;

	do {
		if (archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS) != ARCHIVE_OK) {
			pkg_emit_error("archive_read_extract(): %s",
						   archive_error_string(a));
			retcode = EPKG_FATAL;
			break;
		}

		/*
		 * if the file is a configuration file and the configuration
		 * file does not already exist on the file system, then
		 * extract it
		 * ex: conf1.cfg.pkgconf:
		 * if conf1.cfg doesn't exists create it based on
		 * conf1.cfg.pkgconf
		 */
		if (is_conf_file(archive_entry_pathname(ae), path, sizeof(path))
		    && lstat(path, &st) == ENOENT) {
			archive_entry_set_pathname(ae, path);
			if (archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS) != ARCHIVE_OK) {
				pkg_emit_error("archive_read_extract(): %s",
							   archive_error_string(a));
				retcode = EPKG_FATAL;
				break;
			}
		}
	} while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK);

	if (ret != ARCHIVE_EOF) {
		pkg_emit_error("archive_read_next_header(): %s",
					   archive_error_string(a));
		retcode = EPKG_FATAL;
	}

	return (retcode);
}

int
pkg_add(struct pkgdb *db, const char *path)
{
	return (pkg_add2(db, path, 0, 0));
}

int
pkg_add2(struct pkgdb *db, const char *path, int upgrade, int automatic)
{
	struct archive *a;
	struct archive_entry *ae;
	struct pkgdb_it *it;
	struct pkg *p = NULL;
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	struct utsname u;
	bool extract = true;
	char dpath[MAXPATHLEN + 1];
	const char *basedir;
	const char *ext;
	int retcode = EPKG_OK;
	int ret;

	assert(path != NULL);

	/*
	 * Open the package archive file, read all the meta files and set the
	 * current archive_entry to the first non-meta file.
	 * If there is no non-meta files, EPKG_END is returned.
	 */
	ret = pkg_open2(&pkg, &a, &ae, path, NULL);
	if (ret == EPKG_END)
		extract = false;
	else if (ret != EPKG_OK) {
		retcode = ret;
		goto cleanup;
	}

	if (automatic == 1)
		pkg_setautomatic(pkg);

	if (uname(&u) != 0) {
		pkg_emit_errno("uname", "");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	/*
	 * Check the architecture
	 */
	if (strcmp(u.machine, pkg_get(pkg, PKG_ARCH)) != 0) {
		pkg_emit_error("wrong architecture: %s instead of %s",
					   pkg_get(pkg, PKG_ARCH), u.machine);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	/*
	 * TODO: check the os version
	 */

	/*
	 * Check if the package is already installed
	 */
	it = pkgdb_query(db, pkg_get(pkg, PKG_ORIGIN), MATCH_EXACT);
	if (it == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	ret = pkgdb_it_next(it, &p, PKG_LOAD_BASIC);
	pkgdb_it_free(it);

	if (ret == EPKG_OK) {
		pkg_emit_already_installed(pkg);
		retcode = EPKG_INSTALLED;
		goto cleanup;
	} else if (ret != EPKG_END) {
		retcode = ret;
		goto cleanup;
	}

	/*
	 * Check for dependencies
	 */

	basedir = dirname(path);
	if ((ext = strrchr(path, '.')) == NULL) {
		pkg_emit_error("%s has no extension", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (dep_installed(dep, db) != EPKG_OK) {
			snprintf(dpath, sizeof(dpath), "%s/%s-%s%s", basedir,
					 pkg_dep_name(dep), pkg_dep_version(dep),
					 ext);

			if (access(dpath, F_OK) == 0) {
				if (pkg_add2(db, dpath, 0, 1) != EPKG_OK) {
					retcode = EPKG_FATAL;
					goto cleanup;
				}
			} else {
				retcode = EPKG_FATAL;
				pkg_emit_missing_dep(pkg, dep);
				goto cleanup;
			}
		}
	}

	/* Register the package before installing it in case there are
	 * problems that could be caught here. */
	retcode = pkgdb_register_pkg(db, pkg);
	if (retcode != EPKG_OK || pkgdb_has_flag(db, PKGDB_FLAG_IN_FLIGHT) == 0)
		goto cleanup_reg;

	if (!upgrade)
		pkg_emit_install_begin(pkg);

	/*
	 * Execute pre-install scripts
	 */
	if (!upgrade)
		pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL);

	/*
	 * Extract the files on disk.
	 */
	if (extract == true && (retcode = do_extract(a, ae)) != EPKG_OK) {
		/* If the add failed, clean up */
		pkg_delete_files(pkg, 1);
		pkg_delete_dirs(db, pkg, 1);
		goto cleanup_reg;
	}

	/*
	 * Execute post install scripts
	 */
	if (upgrade)
		pkg_script_run(pkg, PKG_SCRIPT_POST_UPGRADE);
	else
		pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL);

	if (upgrade)
		pkg_emit_upgrade_finished(pkg);
	else
		pkg_emit_install_finished(pkg);

	cleanup_reg:
	pkgdb_register_finale(db, retcode);

	cleanup:
	if (a != NULL)
		archive_read_finish(a);

	if (p != NULL)
		pkg_free(p);

	pkg_free(pkg);

	return (retcode);
}
