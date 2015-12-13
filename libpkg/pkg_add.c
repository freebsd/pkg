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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <glob.h>
#include <pwd.h>
#include <grp.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

#if defined(UF_NOUNLINK)
#define NOCHANGESFLAGS	(UF_IMMUTABLE | UF_APPEND | UF_NOUNLINK | SF_IMMUTABLE | SF_APPEND | SF_NOUNLINK)
#else
#define NOCHANGESFLAGS	(UF_IMMUTABLE | UF_APPEND | SF_IMMUTABLE | SF_APPEND)
#endif


static const unsigned char litchar[] =
"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static void
pkg_add_file_random_suffix(char *buf, int buflen, int suflen)
{
	int nchars = strlen(buf);
	char *pos;
	int r;

	if (nchars + suflen > buflen - 1) {
		suflen = buflen - nchars - 1;
		if (suflen <= 0)
			return;
	}

	buf[nchars++] = '.';
	pos = buf + nchars;

	while(suflen --) {
#ifndef HAVE_ARC4RANDOM
		r = rand() % (sizeof(litchar) - 1);
#else
		r = arc4random_uniform(sizeof(litchar) - 1);
#endif
		*pos++ = litchar[r];
	}

	*pos = '\0';
}

static void
attempt_to_merge(bool renamed, struct pkg_config_file *rcf,
  struct pkg *local, char *pathname, const char *path, struct sbuf *newconf)
{
	const struct pkg_file *lf = NULL;
	struct pkg_config_file *lcf = NULL;

	char *localconf = NULL;
	size_t sz;
	char *localsum;

	if (!renamed) {
		pkg_debug(3, "Not renamed");
		return;
	}

	if (rcf == NULL) {
		pkg_debug(3, "No remote config file");
		return;
	}

	if (local == NULL) {
		pkg_debug(3, "No local package");
		return;
	}

	if (!pkg_is_config_file(local, path, &lf, &lcf)) {
		pkg_debug(3, "No local package");
		return;
	}

	if (lcf->content == NULL) {
		pkg_debug(3, "Empty configuration content for local package");
		return;
	}
	
	pkg_debug(1, "Config file found %s", pathname);
	file_to_buffer(pathname, &localconf, &sz);

	pkg_debug(2, "size: %d vs %d", sz, strlen(lcf->content));

	if (sz == strlen(lcf->content)) {
		pkg_debug(2, "Ancient vanilla and deployed conf are the same size testing checksum");
		localsum = pkg_checksum_data(localconf, sz,
		    PKG_HASH_TYPE_SHA256_HEX);
		if (localsum && strcmp(localsum, lf->sum) == 0) {
			pkg_debug(2, "Checksum are the same %d", strlen(localconf));
			free(localconf);
			free(localsum);
			return;
		}
		free(localsum);
		pkg_debug(2, "Checksum are different %d", strlen(localconf));
	}

	pkg_debug(1, "Attempting to merge %s", pathname);
	if (merge_3way(lcf->content, localconf, rcf->content, newconf) != 0) {
		pkg_emit_error("Impossible to merge configuration file");
		sbuf_clear(newconf);
		strlcat(pathname, ".pkgnew", MAXPATHLEN);
	}
	free(localconf);
}

static uid_t
get_uid_from_archive(struct archive_entry *ae)
{
	char buffer[128];
	struct passwd pwent, *result;

	if ((getpwnam_r(archive_entry_uname(ae), &pwent, buffer, sizeof(buffer),
	    &result)) < 0)
		return (0);
	if (result == NULL)
		return (0);
	return (pwent.pw_uid);
}

static gid_t
get_gid_from_archive(struct archive_entry *ae)
{
	char buffer[128];
	struct group grent, *result;

	if ((getgrnam_r(archive_entry_gname(ae), &grent, buffer, sizeof(buffer),
	    &result)) < 0)
		return (0);
	if (result == NULL)
		return (0);
	return (grent.gr_gid);
}

static int
do_extract(struct archive *a, struct archive_entry *ae, const char *location,
    int nfiles, struct pkg *pkg, struct pkg *local)
{
	int	retcode = EPKG_OK;
	int	ret = 0, cur_file = 0;
	char	path[MAXPATHLEN], pathname[MAXPATHLEN], rpath[MAXPATHLEN];
	char	bd[MAXPATHLEN], *cp;
	struct stat st;
	const struct stat *aest;
	bool renamed = false;
	const struct pkg_file *rf;
	struct pkg_config_file *rcf;
	struct sbuf *newconf;
	bool automerge = pkg_object_bool(pkg_config_get("AUTOMERGE"));
	unsigned long set, clear;

#ifndef HAVE_ARC4RANDOM
	srand(time(NULL));
#endif

	if (nfiles == 0)
		return (EPKG_OK);

	pkg_emit_extract_begin(pkg);
	pkg_emit_progress_start(NULL);

	newconf = sbuf_new_auto();

	do {
		ret = ARCHIVE_OK;
		sbuf_clear(newconf);
		rf = NULL;
		rcf = NULL;
		pkg_absolutepath(archive_entry_pathname(ae), path, sizeof(path));
		snprintf(pathname, sizeof(pathname), "%s%s%s",
		    location ? location : "", *path == '/' ? "" : "/",
		    path
		);
		strlcpy(rpath, pathname, sizeof(rpath));

		aest = archive_entry_stat(ae);
		archive_entry_fflags(ae, &set, &clear);
		if (lstat(rpath, &st) != -1) {
			/*
			 * We have an existing file on the path, so handle it
			 */
			if (!S_ISDIR(aest->st_mode)) {
				pkg_debug(2, "Old version found, renaming");
				pkg_add_file_random_suffix(rpath, sizeof(rpath), 12);
				renamed = true;
			}

			if (!S_ISDIR(st.st_mode) && S_ISDIR(aest->st_mode)) {
				if (S_ISLNK(st.st_mode)) {
					if (stat(rpath, &st) == -1) {
						pkg_emit_error("Dead symlink %s", rpath);
					} else {
						pkg_debug(2, "Directory is a symlink, use it");
						pkg_emit_progress_tick(cur_file++, nfiles);
						continue;
					}
				}
			}
		}

		archive_entry_set_pathname(ae, rpath);

		/* load in memory the content of config files */
		if (pkg_is_config_file(pkg, path, &rf, &rcf)) {
			pkg_debug(1, "Populating config_file %s", pathname);
			size_t len = archive_entry_size(ae);
			rcf->content = malloc(len + 1);
			archive_read_data(a, rcf->content, len);
			rcf->content[len] = '\0';
			if (renamed && (!automerge || local == NULL))
				strlcat(pathname, ".pkgnew", sizeof(pathname));
		}

		/*
		 * check if the file is already provided by previous package
		 */
		if (automerge)
			attempt_to_merge(renamed, rcf, local, pathname, path, newconf);

		if (sbuf_len(newconf) == 0 && (rcf == NULL || rcf->content == NULL)) {
			pkg_debug(1, "Extracting: %s", archive_entry_pathname(ae));
			int install_as_user = (getenv("INSTALL_AS_USER") != NULL);
			int extract_flags = EXTRACT_ARCHIVE_FLAGS;
			if (install_as_user) {
				/* when installing as user don't try to set file ownership */
				extract_flags &= ~ARCHIVE_EXTRACT_OWNER;
			}
			ret = archive_read_extract(a, ae, extract_flags);
		} else {
			if (sbuf_len(newconf) == 0) {
				sbuf_cat(newconf, rcf->content);
				sbuf_finish(newconf);
			}
			pkg_debug(2, "Writing conf in %s", pathname);
			unlink(rpath);
			strlcpy(bd, rpath, sizeof(bd));
			if ((cp = strrchr(bd, '/')) != NULL)
				*cp = '\0';
			if (mkdirs(bd) != EPKG_OK) {
				pkg_emit_error("mkdirs(%s)", bd);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			FILE *f = fopen(rpath, "w+");
			if (!f) {
				pkg_emit_error("fopen() for write: %s", rpath);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			fprintf(f, "%s", sbuf_data(newconf));
			fclose(f);
		}

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

		/* Rename old file */
		if (renamed) {

			pkg_debug(1, "Renaming %s -> %s", rpath, pathname);
#ifdef HAVE_CHFLAGS
			bool old = false;
			if (set & NOCHANGESFLAGS)
				lchflags(rpath, 0);

			if (lstat(pathname, &st) != -1) {
				old = true;
				if (st.st_flags & NOCHANGESFLAGS)
					lchflags(pathname, 0);
			}
#endif

			if (rename(rpath, pathname) == -1) {
#ifdef HAVE_CHFLAGS
				/* restore flags */
				if (old)
					lchflags(pathname, st.st_flags);
#endif
				pkg_emit_error("cannot rename %s to %s: %s", rpath, pathname,
					strerror(errno));
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}
		/* enforce modes and creds */
		lchmod(pathname, archive_entry_perm(ae));
		if (getenv("INSTALL_AS_USER") == NULL) {
			lchown(pathname, get_uid_from_archive(ae),
			    get_gid_from_archive(ae));
		}
#ifdef HAVE_CHFLAGS
		/* Restore flags */
		lchflags(pathname, set);
#endif

		if (string_end_with(pathname, ".pkgnew"))
			pkg_emit_notice("New configuration file: %s", pathname);

		renamed = false;
	} while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK);

	if (ret != ARCHIVE_EOF) {
		pkg_emit_error("archive_read_next_header(): %s",
		    archive_error_string(a));
		retcode = EPKG_FATAL;
	}

cleanup:

	pkg_emit_progress_tick(nfiles, nfiles);
	pkg_emit_extract_finished(pkg);

	if (renamed && retcode == EPKG_FATAL) {
#ifdef HAVE_CHFLAGS
		if (set & NOCHANGESFLAGS)
			chflags(rpath, set & ~NOCHANGESFLAGS);
#endif
		unlink(rpath);
	}

	return (retcode);
}

static char *
pkg_globmatch(char *pattern, const char *name)
{
	glob_t g;
	int i;
	char *buf, *buf2;
	char *path = NULL;

	if (glob(pattern, 0, NULL, &g) == GLOB_NOMATCH) {
		globfree(&g);

		return (NULL);
	}

	for (i = 0; i < g.gl_pathc; i++) {
		/* the version starts here */
		buf = strrchr(g.gl_pathv[i], '-');
		if (buf == NULL)
			continue;
		buf2 = strchr(g.gl_pathv[i], '/');
		if (buf2 == NULL)
			buf2 = g.gl_pathv[i];
		else
			buf2++;
		/* ensure we have match the proper name */
		if (strncmp(buf2, name, buf - buf2) != 0)
			continue;
		if (path == NULL) {
			path = g.gl_pathv[i];
			continue;
		}
		if (pkg_version_cmp(path, g.gl_pathv[i]) == 1)
			path = g.gl_pathv[i];
	}
	path = strdup(path);
	globfree(&g);

	return (path);
}

static int
pkg_add_check_pkg_archive(struct pkgdb *db, struct pkg *pkg,
	const char *path, int flags,
	struct pkg_manifest_key *keys, const char *location)
{
	const char	*arch;
	int	ret, retcode;
	struct pkg_dep	*dep = NULL;
	char	bd[MAXPATHLEN], *basedir;
	char	dpath[MAXPATHLEN], *ppath;
	const char	*ext;
	struct pkg	*pkg_inst = NULL;

	arch = pkg->abi != NULL ? pkg->abi : pkg->arch;

	if (!is_valid_abi(arch, true)) {
		if ((flags & PKG_ADD_FORCE) == 0) {
			return (EPKG_FATAL);
		}
	}

	/* XX check */
	ret = pkg_try_installed(db, pkg->name, &pkg_inst, PKG_LOAD_BASIC);
	if (ret == EPKG_OK) {
		if ((flags & PKG_ADD_FORCE) == 0) {
			pkg_emit_already_installed(pkg_inst);
			pkg_free(pkg_inst);
			pkg_inst = NULL;
			return (EPKG_INSTALLED);
		}
		else if (pkg_inst->locked) {
			pkg_emit_locked(pkg_inst);
			pkg_free(pkg_inst);
			pkg_inst = NULL;
			return (EPKG_LOCKED);
		}
		else {
			pkg_emit_notice("package %s is already installed, forced install",
				pkg->name);
			pkg_free(pkg_inst);
			pkg_inst = NULL;
		}
	} else if (ret != EPKG_END) {
		return (ret);
	}

	/*
	 * Check for dependencies by searching the same directory as
	 * the package archive we're reading.  Of course, if we're
	 * reading from a file descriptor or a unix domain socket or
	 * whatever, there's no valid directory to search.
	 */
	strlcpy(bd, path, sizeof(bd));
	if (strncmp(path, "-", 2) != 0) {
		basedir = dirname(bd);
		if ((ext = strrchr(path, '.')) == NULL) {
			pkg_emit_error("%s has no extension", path);
			return (EPKG_FATAL);
		}
	} else {
		ext = NULL;
		basedir = NULL;
	}

	retcode = EPKG_FATAL;
	pkg_emit_add_deps_begin(pkg);

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (pkg_is_installed(db, dep->name) == EPKG_OK)
			continue;

		if (basedir == NULL) {
			pkg_emit_missing_dep(pkg, dep);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
			continue;
		}

		if (dep->version != NULL && dep->version[0] != '\0') {
			snprintf(dpath, sizeof(dpath), "%s/%s-%s%s", basedir,
				dep->name, dep->version, ext);

			if ((flags & PKG_ADD_UPGRADE) == 0 &&
			    access(dpath, F_OK) == 0) {
				ret = pkg_add(db, dpath, PKG_ADD_AUTOMATIC,
				    keys, location);

				if (ret != EPKG_OK)
					goto cleanup;
			} else {
				pkg_emit_missing_dep(pkg, dep);
				if ((flags & PKG_ADD_FORCE_MISSING) == 0)
					goto cleanup;
			}
		} else {
			snprintf(dpath, sizeof(dpath), "%s/%s-*%s", basedir,
			    dep->name, ext);
			ppath = pkg_globmatch(dpath, dep->name);
			if (ppath == NULL) {
				pkg_emit_missing_dep(pkg, dep);
				if ((flags & PKG_ADD_FORCE_MISSING) == 0)
					goto cleanup;
				continue;
			}
			if ((flags & PKG_ADD_UPGRADE) == 0 &&
			    access(ppath, F_OK) == 0) {
				ret = pkg_add(db, ppath, PKG_ADD_AUTOMATIC,
				    keys, location);

				free(ppath);
				if (ret != EPKG_OK)
					goto cleanup;
			} else {
				free(ppath);
				pkg_emit_missing_dep(pkg, dep);
				if ((flags & PKG_ADD_FORCE_MISSING) == 0)
					goto cleanup;
				continue;
			}
		}
	}

	retcode = EPKG_OK;
cleanup:
	pkg_emit_add_deps_finished(pkg);

	return (retcode);
}

static int
pkg_add_cleanup_old(struct pkgdb *db, struct pkg *old, struct pkg *new, int flags)
{
	struct pkg_file *f;
	int ret = EPKG_OK;
	bool handle_rc;

	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));
	if (handle_rc)
		pkg_start_stop_rc_scripts(old, PKG_RC_STOP);

	/*
	 * Execute pre deinstall scripts
	 */
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		if ((flags & PKG_ADD_USE_UPGRADE_SCRIPTS) == PKG_ADD_USE_UPGRADE_SCRIPTS)
			ret = pkg_script_run(old, PKG_SCRIPT_PRE_UPGRADE);
		else
			ret = pkg_script_run(old, PKG_SCRIPT_PRE_DEINSTALL);
		if (ret != EPKG_OK)
			return (ret);
	}

	/* Now remove files that no longer exist in the new package */
	if (new != NULL) {
		f = NULL;
		while (pkg_files(old, &f) == EPKG_OK) {
			if (!pkg_has_file(new, f->path)) {
				pkg_debug(2, "File %s is not in the new package", f->path);
				pkg_delete_file(old, f, flags & PKG_DELETE_FORCE ? 1 : 0);
			}
		}

		pkg_delete_dirs(db, old, new);
	}

	return (ret);
}

static int
pkg_add_common(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *reloc, struct pkg *remote,
    struct pkg *local)
{
	struct archive		*a;
	struct archive_entry	*ae;
	struct pkg		*pkg = NULL;
	struct sbuf		*message;
	struct pkg_message	*msg;
	const char		*location, *msgstr;
	bool			 extract = true;
	bool			 handle_rc = false;
	int			 retcode = EPKG_OK;
	int			 ret;
	int			 nfiles;

	assert(path != NULL);

	if (local != NULL)
		flags |= PKG_ADD_UPGRADE;

	location = reloc;
	if (pkg_rootdir != NULL)
		location = pkg_rootdir;

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
	if ((flags & PKG_ADD_SPLITTED_UPGRADE) != PKG_ADD_SPLITTED_UPGRADE)
		pkg_emit_new_action();
	if ((flags & PKG_ADD_UPGRADE) == 0)
		pkg_emit_install_begin(pkg);
	else {
		if (local != NULL)
			pkg_emit_upgrade_begin(pkg, local);
		else
			pkg_emit_install_begin(pkg);
	}

	if (pkg_is_valid(pkg) != EPKG_OK) {
		pkg_emit_error("the package is not valid");
		return (EPKG_FATAL);
	}

	if (flags & PKG_ADD_AUTOMATIC)
		pkg->automatic = true;

	/*
	 * Additional checks for non-remote package
	 */
	if (remote == NULL) {
		ret = pkg_add_check_pkg_archive(db, pkg, path, flags, keys,
		    location);
		if (ret != EPKG_OK) {
			/* Do not return error on installed package */
			retcode = (ret == EPKG_INSTALLED ? EPKG_OK : ret);
			goto cleanup;
		}
	}
	else {
		if (remote->repo != NULL) {
			/* Save reponame */
			pkg_kv_add(&pkg->annotations, "repository", remote->repo->name, "annotation");
			pkg_kv_add(&pkg->annotations, "repo_type", remote->repo->ops->type, "annotation");
		}

		free(pkg->digest);
		pkg->digest = strdup(remote->digest);
		/* only preserve flags is -A has not been passed */
		if ((flags & PKG_ADD_AUTOMATIC) == 0)
			pkg->automatic = remote->automatic;
	}

	if (pkg_rootdir == NULL && location != NULL)
		pkg_kv_add(&pkg->annotations, "relocated", location, "annotation");

	/* register the package before installing it in case there are
	 * problems that could be caught here. */
	retcode = pkgdb_register_pkg(db, pkg,
			flags & PKG_ADD_UPGRADE,
			flags & PKG_ADD_FORCE);

	if (retcode != EPKG_OK)
		goto cleanup;

	if (local != NULL) {
		pkg_debug(1, "Cleaning up old version");
		if (pkg_add_cleanup_old(db, local, pkg, flags) != EPKG_OK) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}

	/*
	 * Execute pre-install scripts
	 */
	if ((flags & (PKG_ADD_NOSCRIPT | PKG_ADD_USE_UPGRADE_SCRIPTS)) == 0)
		pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL);

	/* add the user and group if necessary */

	nfiles = kh_count(pkg->filehash);
	/*
	 * Extract the files on disk.
	 */
	if (extract &&
	    (retcode = do_extract(a, ae, location, nfiles, pkg, local))
	    != EPKG_OK) {
		/* If the add failed, clean up (silently) */
		pkg_delete_files(pkg, 2);
		pkg_delete_dirs(db, pkg, NULL);
		goto cleanup_reg;
	}

	/* Update configuration file content with db with newer versions */
	pkgdb_update_config_file_content(pkg, db->sqlite);

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

	if (retcode == EPKG_OK) {
		if ((flags & PKG_ADD_UPGRADE) == 0)
			pkg_emit_install_finished(pkg, local);
		else {
			if (local != NULL)
				pkg_emit_upgrade_finished(pkg, local);
			else
				pkg_emit_install_finished(pkg, local);
		}

		if (pkg->message != NULL)
			message = sbuf_new_auto();
		LL_FOREACH(pkg->message, msg) {
			msgstr = NULL;
			if (msg->type == PKG_MESSAGE_ALWAYS) {
				msgstr = msg->str;
			} else if (local != NULL &&
			     msg->type == PKG_MESSAGE_UPGRADE) {
				if (msg->maximum_version == NULL &&
				    msg->minimum_version == NULL) {
					msgstr = msg->str;
				} else if (msg->maximum_version == NULL) {
					if (pkg_version_cmp(local->version, msg->minimum_version) == 1) {
						msgstr = msg->str;
					}
				} else if (msg->minimum_version == NULL) {
					if (pkg_version_cmp(local->version, msg->maximum_version) == -1) {
						msgstr = msg->str;
					}
				} else if (pkg_version_cmp(local->version, msg->maximum_version) == -1 &&
					    pkg_version_cmp(local->version, msg->minimum_version) == 1) {
					msgstr = msg->str;
				}
			} else if (local == NULL &&
			    msg->type == PKG_MESSAGE_INSTALL) {
				msgstr = msg->str;
			}
			if (msgstr != NULL) {
				if (sbuf_len(message) == 0) {
					pkg_sbuf_printf(message, "Message from "
					    "%n-%v:\n", pkg, pkg);
				}
				sbuf_printf(message, "%s\n", msgstr);
			}
		}
		if (pkg->message != NULL) {
			if (sbuf_len(message) > 0) {
				sbuf_finish(message);
				pkg_emit_message(sbuf_data(message));
			}
			sbuf_delete(message);
		}
	}

	cleanup:
	if (a != NULL) {
		archive_read_close(a);
		archive_read_free(a);
	}

	pkg_free(pkg);

	return (retcode);
}

int
pkg_add(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location)
{
	return pkg_add_common(db, path, flags, keys, location, NULL, NULL);
}

int
pkg_add_from_remote(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location, struct pkg *rp)
{
	return pkg_add_common(db, path, flags, keys, location, rp, NULL);
}

int
pkg_add_upgrade(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location,
    struct pkg *rp, struct pkg *lp)
{
	if (pkgdb_ensure_loaded(db, lp,
	    PKG_LOAD_FILES|PKG_LOAD_SCRIPTS|PKG_LOAD_DIRS) != EPKG_OK)
		return (EPKG_FATAL);

	return pkg_add_common(db, path, flags, keys, location, rp, lp);
}
