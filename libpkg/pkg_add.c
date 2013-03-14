/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/utsname.h>

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

static int
dep_installed(struct pkg_dep *dep, struct pkgdb *db) {
	struct pkg *p = NULL;
	struct pkgdb_it *it;
	int ret = EPKG_FATAL;

	it = pkgdb_query(db, pkg_dep_origin(dep), MATCH_EXACT);

	if (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK)
		ret = EPKG_OK;

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
		const char *pathname = archive_entry_pathname(ae);

		ret = archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
		if (ret != ARCHIVE_OK) {
			/*
			 * show error except when the failure is during
			 * extracting a directory and that the directory already
			 * exists.
			 * this allow to install packages linux_base from
			 * package for example
			 */
			if (archive_entry_filetype(ae) != AE_IFDIR ||
			    !is_dir(pathname)) {
				pkg_emit_error("archive_read_extract(): %s",
				    archive_error_string(a));
				retcode = EPKG_FATAL;
				break;
			}
		}

		/*
		 * if the file is a configuration file and the configuration
		 * file does not already exist on the file system, then
		 * extract it
		 * ex: conf1.cfg.pkgconf:
		 * if conf1.cfg doesn't exists create it based on
		 * conf1.cfg.pkgconf
		 */
		if (is_conf_file(pathname, path, sizeof(path))
		    && lstat(path, &st) == -1 && errno == ENOENT) {
			archive_entry_set_pathname(ae, path);
			ret = archive_read_extract(a,ae, EXTRACT_ARCHIVE_FLAGS);
			if (ret != ARCHIVE_OK) {
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

static int
do_extract_mtree(char *mtree, const char *prefix)
{
	struct archive *a = NULL;
	struct archive_entry *ae;
	char path[MAXPATHLEN];
	const char *fpath;
	int retcode = EPKG_OK;
	int ret;

	if (mtree == NULL || *mtree == '\0')
		return EPKG_OK;

	a = archive_read_new();
	archive_read_support_filter_none(a);
	archive_read_support_format_mtree(a);

	if (archive_read_open_memory(a, mtree, strlen(mtree)) != ARCHIVE_OK) {
		pkg_emit_error("Fail to extract the mtree: %s",
		    archive_error_string(a));
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while ((ret = archive_read_next_header(a, &ae)) != ARCHIVE_EOF) {
		if (ret != ARCHIVE_OK) {
			pkg_emit_error("Skipping unsupported mtree line: %s",
			    archive_error_string(a));
			continue;
		}
		fpath = archive_entry_pathname(ae);

		if (*fpath != '/') {
			snprintf(path, sizeof(path), "%s/%s", prefix, fpath);
			archive_entry_set_pathname(ae, path);
		}

		/* Ignored failed extraction on purpose */
		archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
	}

cleanup:
	if (a != NULL)
		archive_read_free(a);

	return (retcode);
}

int
pkg_add(struct pkgdb *db, const char *path, int flags)
{
	const char *arch;
	const char *myarch;
	const char *origin;
	struct archive *a;
	struct archive_entry *ae;
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	bool extract = true;
	bool handle_rc = false;
	char dpath[MAXPATHLEN + 1];
	const char *basedir;
	const char *ext;
	int retcode = EPKG_OK;
	int ret;
	char *mtree, *prefix;

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
	if ((flags & PKG_ADD_UPGRADE) == 0)
		pkg_emit_install_begin(pkg);

	if (pkg_is_valid(pkg) != EPKG_OK) {
		pkg_emit_error("the package is not valid");
		return (EPKG_FATAL);
	}

	if (flags & PKG_ADD_AUTOMATIC)
		pkg_set(pkg, PKG_AUTOMATIC, true);

	/*
	 * Check the architecture
	 */

	pkg_config_string(PKG_CONFIG_ABI, &myarch);
	pkg_get(pkg, PKG_ARCH, &arch, PKG_ORIGIN, &origin);

	if (fnmatch(myarch, arch, FNM_CASEFOLD) == FNM_NOMATCH && strncmp(arch, myarch, strlen(myarch)) != 0) {
		pkg_emit_error("wrong architecture: %s instead of %s",
		    arch, myarch);
		if ((flags & PKG_ADD_FORCE) == 0) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}

	/*
	 * Check if the package is already installed
	 */

	ret = pkg_is_installed(db, origin);

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
			const char *dep_name = pkg_dep_name(dep);
			const char *dep_ver = pkg_dep_version(dep);

			snprintf(dpath, sizeof(dpath), "%s/%s-%s%s", basedir,
			    dep_name, dep_ver, ext);

			if ((flags & PKG_ADD_UPGRADE) == 0 &&
			    access(dpath, F_OK) == 0) {
				ret = pkg_add(db, dpath, PKG_ADD_AUTOMATIC);
				if (ret != EPKG_OK) {
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

	/* register the package before installing it in case there are
	 * problems that could be caught here. */
	if ((flags & PKG_ADD_UPGRADE) == 0)
		retcode = pkgdb_register_pkg(db, pkg, 0);
	else
		retcode = pkgdb_register_pkg(db, pkg, 1);

	if (retcode != EPKG_OK)
		goto cleanup;

	pkg_get(pkg, PKG_PREFIX, &prefix, PKG_MTREE, &mtree);
	if ((retcode = do_extract_mtree(mtree, prefix)) != EPKG_OK)
		goto cleanup_reg;

	/*
	 * Execute pre-install scripts
	 */
	if ((flags & PKG_ADD_USE_UPGRADE_SCRIPTS) == 0)
		pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL);

	/* add the user and group if necessary */
	/* pkg_add_user_group(pkg); */

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
	if (flags & PKG_ADD_USE_UPGRADE_SCRIPTS)
		pkg_script_run(pkg, PKG_SCRIPT_POST_UPGRADE);
	else
		pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL);

	/*
	 * start the different related services if the users do want that
	 * and that the service is running
	 */

	pkg_config_bool(PKG_CONFIG_HANDLE_RC_SCRIPTS, &handle_rc);
	if (handle_rc)
		pkg_start_stop_rc_scripts(pkg, PKG_RC_START);

	cleanup_reg:
	if ((flags & PKG_ADD_UPGRADE) == 0)
		pkgdb_register_finale(db, retcode);

	if (retcode == EPKG_OK && (flags & PKG_ADD_UPGRADE) == 0)
		pkg_emit_install_finished(pkg);

	cleanup:
	if (a != NULL)
		archive_read_free(a);

	pkg_free(pkg);

	return (retcode);
}
