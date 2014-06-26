/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"

static int
do_extract(struct archive *a, struct archive_entry *ae, const char *location,
		int nfiles, struct pkg *pkg)
{
	int	retcode = EPKG_OK;
	int	ret = 0, cur_file = 0;
	char	path[MAXPATHLEN], pathname[MAXPATHLEN], rpath[MAXPATHLEN];
	struct stat st;
	const char *name;
	bool renamed = true;

	pkg_get(pkg, PKG_NAME, &name);
	pkg_emit_progress_start(NULL);
	/* show a progression on package with no files */
	if (nfiles == 0)
		pkg_emit_progress_tick(1,1);

	do {
		snprintf(pathname, sizeof(pathname), "%s/%s",
		    location ? location : "",
		    archive_entry_pathname(ae)
		);

		if (stat(pathname, &st) != -1 && !S_ISDIR(st.st_mode)) {
			/*
			 * We have an existing file on the path, so handle it
			 */

			snprintf(rpath, sizeof(rpath), "%s.%jd", pathname,
			    (intmax_t)time(NULL));
			if (rename(pathname, rpath) == -1) {
				pkg_emit_error("cannot rename %s to %s: %s", pathname, rpath,
				    strerror(errno));
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			renamed = true;
		}

		archive_entry_set_pathname(ae, pathname);

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
				goto cleanup;
			}
		}
		pkg_emit_progress_tick(cur_file++, nfiles);

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
				goto cleanup;
			}
		}
		if (renamed) {
			unlink(rpath);
			renamed = false;
		}
	} while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK);

	if (ret != ARCHIVE_EOF) {
		pkg_emit_error("archive_read_next_header(): %s",
		    archive_error_string(a));
		retcode = EPKG_FATAL;
	}

cleanup:

	pkg_emit_progress_tick(nfiles, nfiles);

	if (renamed && retcode == EPKG_FATAL)
		rename(rpath, pathname);

	return (retcode);
}

int
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

static int
pkg_add_common(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location, struct pkg *remote)
{
	const char	*arch;
	const char	*origin;
	const char	*name;
	struct archive	*a;
	struct archive_entry *ae;
	struct pkg	*pkg = NULL;
	struct pkg_dep	*dep = NULL;
	struct pkg      *pkg_inst = NULL;
	bool		 extract = true;
	bool		 handle_rc = false;
	bool		 disable_mtree;
	char		 dpath[MAXPATHLEN];
	const char	*basedir;
	const char	*ext;
	char		*mtree;
	char		*prefix;
	int		 retcode = EPKG_OK;
	int		 ret;
	int nfiles;

	assert(path != NULL);

	/*
	 * Open the package archive file, read all the meta files and set the
	 * current archive_entry to the first non-meta file.
	 * If there is no non-meta files, EPKG_END is returned.
	 */
	ret = pkg_open2(&pkg, &a, &ae, path, keys, 0, -1);
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
		pkg_set(pkg, PKG_AUTOMATIC, (bool)true);

	/*
	 * Check the architecture
	 */
	pkg_get(pkg, PKG_ARCH, &arch, PKG_ORIGIN, &origin, PKG_NAME, &name);

	/*
	 * Additional checks for non-remote package
	 */
	if (remote == NULL) {
		if (!is_valid_abi(arch, true)) {
			if ((flags & PKG_ADD_FORCE) == 0) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}
		ret = pkg_try_installed(db, origin, &pkg_inst, PKG_LOAD_BASIC);
		if (ret == EPKG_OK) {
			if ((flags & PKG_ADD_FORCE) == 0) {
				pkg_emit_already_installed(pkg_inst);
				retcode = EPKG_INSTALLED;
				pkg_free(pkg_inst);
				pkg_inst = NULL;
				goto cleanup;
			}
			else {
				pkg_emit_notice("package %s is already installed, forced install",
						name);
				pkg_free(pkg_inst);
				pkg_inst = NULL;
			}
		} else if (ret != EPKG_END) {
			retcode = ret;
			goto cleanup;
		}

		/*
		 * Check for dependencies by searching the same directory as
		 * the package archive we're reading.  Of course, if we're
		 * reading from a file descriptor or a unix domain socket or
		 * somesuch, there's no valid directory to search.
		 */

		if (strncmp(path, "-", 2) != 0 && (flags & PKG_ADD_UPGRADE) == 0) {
			basedir = dirname(path);
			if ((ext = strrchr(path, '.')) == NULL) {
				pkg_emit_error("%s has no extension", path);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		} else {
			basedir = NULL;
			ext = NULL;
		}

		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			if (pkg_is_installed(db, pkg_dep_origin(dep)) == EPKG_OK)
				continue;

			if (basedir != NULL) {
				const char *dep_name = pkg_dep_name(dep);
				const char *dep_ver = pkg_dep_version(dep);

				snprintf(dpath, sizeof(dpath), "%s/%s-%s%s", basedir,
						dep_name, dep_ver, ext);

				if ((flags & PKG_ADD_UPGRADE) == 0 &&
						access(dpath, F_OK) == 0) {
					ret = pkg_add(db, dpath, PKG_ADD_AUTOMATIC, keys, location);
					if (ret != EPKG_OK) {
						retcode = EPKG_FATAL;
						goto cleanup;
					}
				} else {
					pkg_emit_error("Missing dependency matching "
							"Origin: '%s' Version: '%s'",
							pkg_dep_get(dep, PKG_DEP_ORIGIN),
							pkg_dep_get(dep, PKG_DEP_VERSION));
					if ((flags & PKG_ADD_FORCE_MISSING) == 0) {
						retcode = EPKG_FATAL;
						goto cleanup;
					}
				}
			} else {
				retcode = EPKG_FATAL;
				pkg_emit_missing_dep(pkg, dep);
				goto cleanup;
			}
		}
	}
	else {
		const char *manifestdigest;

		if (remote->repo != NULL) {
			/* Save reponame */
			pkg_addannotation(pkg, "repository", remote->repo->name);
			pkg_addannotation(pkg, "repo_type", remote->repo->ops->type);
		}

		pkg_get(remote, PKG_DIGEST, &manifestdigest);
		pkg_set(pkg, PKG_DIGEST, manifestdigest);
	}

	if (location != NULL)
		pkg_addannotation(pkg, "relocated", location);

	/* register the package before installing it in case there are
	 * problems that could be caught here. */
	retcode = pkgdb_register_pkg(db, pkg,
			flags & PKG_ADD_UPGRADE,
			flags & PKG_ADD_FORCE);

	if (retcode != EPKG_OK)
		goto cleanup;

	/* MTREE replicates much of the standard functionality
	 * inplicit in the way pkg works.  It has to remain available
	 * in the ports for compatibility with the old pkg_tools, but
	 * ultimately, MTREE should be made redundant.  Use this for
	 * experimantal purposes and to develop MTREE-free versions of
	 * packages. */

	disable_mtree = pkg_object_bool(pkg_config_get("DISABLE_MTREE"));
	if (!disable_mtree) {
		pkg_get(pkg, PKG_PREFIX, &prefix, PKG_MTREE, &mtree);
		if ((retcode = do_extract_mtree(mtree, prefix)) != EPKG_OK)
			goto cleanup_reg;
	}

	/*
	 * Execute pre-install scripts
	 */
	if ((flags & (PKG_ADD_NOSCRIPT | PKG_ADD_USE_UPGRADE_SCRIPTS)) == 0)
		pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL);

	/* add the user and group if necessary */
	/* pkg_add_user_group(pkg); */

	nfiles = HASH_COUNT(pkg->files);
	/*
	 * Extract the files on disk.
	 */
	if (extract && (retcode = do_extract(a, ae, location, nfiles, pkg)) != EPKG_OK) {
		/* If the add failed, clean up (silently) */
		pkg_delete_files(pkg, 2);
		pkg_delete_dirs(db, pkg, 1);
		goto cleanup_reg;
	}

	/*
	 * Execute post install scripts
	 */
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		if ((flags & PKG_ADD_USE_UPGRADE_SCRIPTS) == PKG_ADD_USE_UPGRADE_SCRIPTS)
			pkg_script_run(pkg, PKG_SCRIPT_POST_UPGRADE);
		else
			pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL);
	}

	/*
	 * start the different related services if the users do want that
	 * and that the service is running
	 */

	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));
	if (handle_rc)
		pkg_start_stop_rc_scripts(pkg, PKG_RC_START);

	cleanup_reg:
	if ((flags & PKG_ADD_UPGRADE) == 0)
		pkgdb_register_finale(db, retcode);

	if (retcode == EPKG_OK && (flags & PKG_ADD_UPGRADE) == 0)
		pkg_emit_install_finished(pkg);

	cleanup:
	if (a != NULL) {
		archive_read_close(a);
		archive_read_free(a);
	}

	pkg_free(pkg);

	if (pkg_inst != NULL)
		pkg_free(pkg_inst);

	return (retcode);
}

int
pkg_add(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location)
{
	return pkg_add_common(db, path, flags, keys, location, NULL);
}

int
pkg_add_from_remote(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location, struct pkg *rp)
{
	return pkg_add_common(db, path, flags, keys, location, rp);
}
