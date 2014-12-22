/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#include <sys/stat.h>

#include <errno.h>
#include <regex.h>
#include <fcntl.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

static int pkg_create_from_dir(struct pkg *, const char *, struct packing *);

static int
pkg_create_from_dir(struct pkg *pkg, const char *root,
    struct packing *pkg_archive)
{
	char		 fpath[MAXPATHLEN];
	struct pkg_file	*file = NULL;
	struct pkg_dir	*dir = NULL;
	int		 ret;
	struct stat	 st;
	char		 sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	int64_t		 flatsize = 0;
	const char	*relocation;
	struct hardlinks *hardlinks = NULL;

	if (pkg_is_valid(pkg) != EPKG_OK) {
		pkg_emit_error("the package is not valid");
		return (EPKG_FATAL);
	}

	relocation = pkg_kv_get(&pkg->annotations, "relocated");
	if (relocation == NULL)
		relocation = "";

	/*
	 * Get / compute size / checksum if not provided in the manifest
	 */
	while (pkg_files(pkg, &file) == EPKG_OK) {

		snprintf(fpath, sizeof(fpath), "%s%s%s", root ? root : "",
		    relocation, file->path);

		if (lstat(fpath, &st) == -1) {
			pkg_emit_error("file '%s' is missing", fpath);
			return (EPKG_FATAL);
		}

		if (file->size == 0)
			file->size = (int64_t)st.st_size;

		if (st.st_nlink == 1 || !check_for_hardlink(&hardlinks, &st)) {
			flatsize += file->size;
		}

		if (S_ISLNK(st.st_mode)) {

			if (file->sum[0] == '\0') {
				if (pkg_symlink_cksum(fpath, root, sha256) == EPKG_OK)
					strlcpy(file->sum, sha256, sizeof(file->sum));
				else
					return (EPKG_FATAL);
			}
		}
		else {
			if (file->sum[0] == '\0') {
				if (sha256_file(fpath, sha256) != EPKG_OK)
					return (EPKG_FATAL);
				strlcpy(file->sum, sha256, sizeof(file->sum));
			}
		}
	}
	pkg->flatsize = flatsize;
	HASH_FREE(hardlinks, free);

	if (pkg->type == PKG_OLD_FILE) {
		pkg_emit_error("Cannot create an old format package");
		return (EPKG_FATAL);
	} else {
		/*
		 * Register shared libraries used by the package if
		 * SHLIBS enabled in conf.  Deletes shlib info if not.
		 */
		struct sbuf *b = sbuf_new_auto();

		pkg_analyse_files(NULL, pkg, root);

		pkg_emit_manifest_sbuf(pkg, b, PKG_MANIFEST_EMIT_COMPACT, NULL);
		packing_append_buffer(pkg_archive, sbuf_data(b), "+COMPACT_MANIFEST", sbuf_len(b));
		sbuf_clear(b);
		pkg_emit_manifest_sbuf(pkg, b, 0, NULL);
		sbuf_finish(b);
		packing_append_buffer(pkg_archive, sbuf_data(b), "+MANIFEST", sbuf_len(b));
		sbuf_delete(b);
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {

		snprintf(fpath, sizeof(fpath), "%s%s%s", root ? root : "",
		    relocation, file->path);

		ret = packing_append_file_attr(pkg_archive, fpath, file->path,
		    file->uname, file->gname, file->perm);
		if (developer_mode && ret != EPKG_OK)
			return (ret);
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		snprintf(fpath, sizeof(fpath), "%s%s%s", root ? root : "",
		    relocation, dir->path);

		ret = packing_append_file_attr(pkg_archive, fpath, dir->path,
		    dir->uname, dir->gname, dir->perm);
		if (developer_mode && ret != EPKG_OK)
			return (ret);
	}

	return (EPKG_OK);
}

static struct packing *
pkg_create_archive(const char *outdir, struct pkg *pkg, pkg_formats format,
    unsigned required_flags)
{
	char		*pkg_path = NULL;
	struct packing	*pkg_archive = NULL;

	/*
	 * Ensure that we have all the information we need
	 */
	if (pkg->type != PKG_OLD_FILE)
		assert((pkg->flags & required_flags) == required_flags);

	if (mkdirs(outdir) != EPKG_OK)
		return NULL;

	if (pkg_asprintf(&pkg_path, "%S/%n-%v", outdir, pkg, pkg) == -1) {
		pkg_emit_errno("pkg_asprintf", "");
		return (NULL);
	}

	if (packing_init(&pkg_archive, pkg_path, format, false) != EPKG_OK)
		pkg_archive = NULL;

	free(pkg_path);

	return pkg_archive;
}

static const char * const scripts[] = {
	"+INSTALL",
	"+PRE_INSTALL",
	"+POST_INSTALL",
	"+POST_INSTALL",
	"+DEINSTALL",
	"+PRE_DEINSTALL",
	"+POST_DEINSTALL",
	"+UPGRADE",
	"+PRE_UPGRADE",
	"+POST_UPGRADE",
	"pkg-install",
	"pkg-pre-install",
	"pkg-post-install",
	"pkg-deinstall",
	"pkg-pre-deinstall",
	"pkg-post-deinstall",
	"pkg-upgrade",
	"pkg-pre-upgrade",
	"pkg-post-upgrade",
	NULL
};


/* The "no concessions to old pkg_tools" variant: just get everything
 * from the manifest */
int
pkg_create_from_manifest(const char *outdir, pkg_formats format,
    const char *rootdir, const char *manifest)
{
	struct pkg	*pkg = NULL;
	struct packing	*pkg_archive = NULL;
	char		 arch[BUFSIZ];
	int		 ret = ENOMEM;
	struct pkg_manifest_key *keys = NULL;

	pkg_debug(1, "Creating package from stage directory: '%s'", rootdir);

	if(pkg_new(&pkg, PKG_FILE) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	pkg_manifest_keys_new(&keys);
	if ((ret = pkg_parse_manifest_file(pkg, manifest, keys)) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	/* if no arch autodetermine it */
	if (pkg->abi == NULL) {
		pkg_get_myarch(arch, BUFSIZ);
		pkg->abi = strdup(arch);
	}

	/* Create the archive */
	pkg_archive = pkg_create_archive(outdir, pkg, format, 0);
	if (pkg_archive == NULL) {
		ret = EPKG_FATAL; /* XXX do better */
		goto cleanup;
	}

	pkg_create_from_dir(pkg, rootdir, pkg_archive);
	ret = EPKG_OK;

cleanup:
	free(pkg);
	pkg_manifest_keys_free(keys);
	if (ret == EPKG_OK)
		ret = packing_finish(pkg_archive);
	return (ret);
}

static void
pkg_load_from_file(int fd, struct pkg *pkg, pkg_attr attr, const char *path)
{

	if (faccessat(fd, path, F_OK, 0) == 0) {
		pkg_debug(1, "Reading: '%s'", path);
		pkg_set_from_fileat(fd, pkg, attr, path, false);
	}
}

int
pkg_create_staged(const char *outdir, pkg_formats format, const char *rootdir,
    const char *md_dir, char *plist)
{
	struct pkg	*pkg = NULL;
	struct pkg_file	*file = NULL;
	struct pkg_dir	*dir = NULL;
	struct packing	*pkg_archive = NULL;
	char		*manifest = NULL;
	char		 arch[BUFSIZ];
	int		 ret = ENOMEM;
	int		 i, mfd;
	regex_t		 preg;
	regmatch_t	 pmatch[2];
	size_t		 size;
	struct pkg_manifest_key *keys = NULL;

	mfd = -1;

	pkg_debug(1, "Creating package from stage directory: '%s'", rootdir);

	if ((mfd = open(md_dir, O_DIRECTORY)) == -1) {
		pkg_emit_errno("open", md_dir);
		goto cleanup;
	}

	if(pkg_new(&pkg, PKG_FILE) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	pkg_manifest_keys_new(&keys);
	/* Load the manifest from the metadata directory */
	if ((ret = pkg_parse_manifest_fileat(mfd, pkg, "+MANIFEST", keys))
	    != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	/* if no descriptions provided then try to get it from a file */
	if (pkg->desc == NULL)
		pkg_load_from_file(mfd, pkg, PKG_DESC, "+DESC");

	/* if no message try to get it from a file */
	if (pkg->message == NULL)
		pkg_load_from_file(mfd, pkg, PKG_MESSAGE, "+DISPLAY");

	/* if no arch autodetermine it */
	if (pkg->abi == NULL) {
		pkg_get_myarch(arch, BUFSIZ);
		pkg->abi = strdup(arch);
	}

	for (i = 0; scripts[i] != NULL; i++) {
		if (faccessat(mfd, scripts[i], F_OK, 0) == 0)
			pkg_addscript_fileat(mfd, pkg, scripts[i]);
	}

	if (plist != NULL &&
	    ports_parse_plist(pkg, plist, rootdir) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	if (pkg->www == NULL) {
		if (pkg->desc == NULL) {
			pkg_emit_error("No www or desc defined in manifest");
			ret = EPKG_FATAL;
			goto cleanup;
		}
		regcomp(&preg, "^WWW:[[:space:]]*(.*)$",
		    REG_EXTENDED|REG_ICASE|REG_NEWLINE);
		if (regexec(&preg, pkg->desc, 2, pmatch, 0) == 0) {
			size = pmatch[1].rm_eo - pmatch[1].rm_so;
			pkg->www = strndup(&pkg->desc[pmatch[1].rm_so], size);
		} else {
			pkg->www = strdup("UNKNOWN");
		}
		regfree(&preg);
	}

	/* Create the archive */
	pkg_archive = pkg_create_archive(outdir, pkg, format, 0);
	if (pkg_archive == NULL) {
		ret = EPKG_FATAL; /* XXX do better */
		goto cleanup;
	}

	/* XXX: autoplist support doesn't work right with meta-ports */
	if (0 && pkg_files(pkg, &file) != EPKG_OK &&
	    pkg_dirs(pkg, &dir) != EPKG_OK) {
		/* Now traverse the file directories, adding to the archive */
		packing_append_tree(pkg_archive, md_dir, NULL);
		packing_append_tree(pkg_archive, rootdir, "/");
		ret = EPKG_OK;
	} else {
		ret = pkg_create_from_dir(pkg, rootdir, pkg_archive);
	}


cleanup:
	if (mfd != -1)
		close(mfd);
	free(pkg);
	free(manifest);
	pkg_manifest_keys_free(keys);
	if (ret == EPKG_OK)
		ret = packing_finish(pkg_archive);
	return (ret);
}

int
pkg_create_installed(const char *outdir, pkg_formats format, struct pkg *pkg)
{
	struct packing	*pkg_archive;

	unsigned	 required_flags = PKG_LOAD_DEPS | PKG_LOAD_FILES |
		PKG_LOAD_CATEGORIES | PKG_LOAD_DIRS | PKG_LOAD_SCRIPTS |
		PKG_LOAD_OPTIONS | PKG_LOAD_LICENSES ;

	assert(pkg->type == PKG_INSTALLED || pkg->type == PKG_OLD_FILE);

	pkg_archive = pkg_create_archive(outdir, pkg, format, required_flags);
	if (pkg_archive == NULL) {
		pkg_emit_error("unable to create archive");
		return (EPKG_FATAL);
	}

	pkg_create_from_dir(pkg, NULL, pkg_archive);

	return packing_finish(pkg_archive);
}
