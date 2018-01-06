/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2014-2015 Matthew Seaman <matthew@FreeBSD.org>
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

#define TICK	100

static int pkg_create_from_dir(struct pkg *, const char *, struct packing *);
static void counter_init(const char *what, int64_t max);
static void counter_count(void);
static void counter_end(void);

extern int osversion;

static int
pkg_create_from_dir(struct pkg *pkg, const char *root,
    struct packing *pkg_archive)
{
	char		 fpath[MAXPATHLEN];
	struct pkg_file	*file = NULL;
	struct pkg_dir	*dir = NULL;
	int		 ret;
	struct stat	 st;
	int64_t		 flatsize = 0;
	int64_t		 nfiles;
	const char	*relocation;
	hardlinks_t	*hardlinks;

	if (pkg_is_valid(pkg) != EPKG_OK) {
		pkg_emit_error("the package is not valid");
		return (EPKG_FATAL);
	}

	relocation = pkg_kv_get(&pkg->annotations, "relocated");
	if (relocation == NULL)
		relocation = "";
	if (pkg_rootdir != NULL)
		relocation = pkg_rootdir;

	/*
	 * Get / compute size / checksum if not provided in the manifest
	 */

	nfiles = kh_count(pkg->filehash);
	counter_init("file sizes/checksums", nfiles);

	hardlinks = kh_init_hardlinks();
	while (pkg_files(pkg, &file) == EPKG_OK) {

		snprintf(fpath, sizeof(fpath), "%s%s%s", root ? root : "",
		    relocation, file->path);

		if (lstat(fpath, &st) == -1) {
			pkg_emit_error("file '%s' is missing", fpath);
			kh_destroy_hardlinks(hardlinks);
			return (EPKG_FATAL);
		}

		if (file->size == 0)
			file->size = (int64_t)st.st_size;

		if (st.st_nlink == 1 || !check_for_hardlink(hardlinks, &st)) {
			flatsize += file->size;
		}

		file->sum = pkg_checksum_generate_file(fpath,
		    PKG_HASH_TYPE_SHA256_HEX);
		if (file->sum == NULL) {
			kh_destroy_hardlinks(hardlinks);
			return (EPKG_FATAL);
		}

		counter_count();
	}
	kh_destroy_hardlinks(hardlinks);

	counter_end();

	pkg->flatsize = flatsize;

	if (pkg->type == PKG_OLD_FILE) {
		pkg_emit_error("Cannot create an old format package");
		return (EPKG_FATAL);
	}
	/*
	 * Register shared libraries used by the package if
	 * SHLIBS enabled in conf.  Deletes shlib info if not.
	 */
	UT_string *b;
	utstring_new(b);

	pkg_emit_manifest_buf(pkg, b, PKG_MANIFEST_EMIT_COMPACT, NULL);
	packing_append_buffer(pkg_archive, utstring_body(b), "+COMPACT_MANIFEST", utstring_len(b));
	utstring_clear(b);
	pkg_emit_manifest_buf(pkg, b, 0, NULL);
	packing_append_buffer(pkg_archive, utstring_body(b), "+MANIFEST", utstring_len(b));
	utstring_free(b);

	counter_init("packing files", nfiles);

	while (pkg_files(pkg, &file) == EPKG_OK) {

		snprintf(fpath, sizeof(fpath), "%s%s%s", root ? root : "",
		    relocation, file->path);

		ret = packing_append_file_attr(pkg_archive, fpath, file->path,
		    file->uname, file->gname, file->perm, file->fflags);
		if (developer_mode && ret != EPKG_OK)
			return (ret);
		counter_count();
	}

	counter_end();

	nfiles = kh_count(pkg->dirhash);
	counter_init("packing directories", nfiles);

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		snprintf(fpath, sizeof(fpath), "%s%s%s", root ? root : "",
		    relocation, dir->path);

		ret = packing_append_file_attr(pkg_archive, fpath, dir->path,
		    dir->uname, dir->gname, dir->perm, dir->fflags);
		if (developer_mode && ret != EPKG_OK)
			return (ret);
		counter_count();
	}

	counter_end();

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

	if (packing_init(&pkg_archive, pkg_path, format) != EPKG_OK)
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
    const char *rootdir, const char *manifest, const char *plist)
{
	struct pkg	*pkg = NULL;
	struct packing	*pkg_archive = NULL;
	int		 ret = ENOMEM;

	pkg_debug(1, "Creating package from stage directory: '%s'", rootdir);

	if(pkg_new(&pkg, PKG_FILE) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	if ((ret = pkg_load_metadata(pkg, manifest, NULL, plist, rootdir, false))
	    != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	/* Create the archive */
	pkg_archive = pkg_create_archive(outdir, pkg, format, 0);
	if (pkg_archive == NULL) {
		ret = EPKG_FATAL; /* XXX do better */
		goto cleanup;
	}

	if ((ret = pkg_create_from_dir(pkg, rootdir, pkg_archive)) != EPKG_OK)
		pkg_emit_error("package creation failed");

cleanup:
	free(pkg);
	packing_finish(pkg_archive);
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

static int
pkg_load_message_from_file(int fd, struct pkg *pkg, const char *path)
{
	char *buf = NULL;
	off_t size = 0;
	int ret;
	ucl_object_t *obj;

	assert(pkg != NULL);
	assert(path != NULL);

	if (faccessat(fd, path, F_OK, 0) == -1) {
		return (EPKG_FATAL);
	}

	pkg_debug(1, "Reading message: '%s'", path);
	if ((ret = file_to_bufferat(fd, path, &buf, &size)) != EPKG_OK) {
		return (ret);
	}

	if (*buf == '[') {
		ret = pkg_message_from_str(pkg, buf, size);
		free(buf);
		return (ret);
	}
	obj = ucl_object_fromstring_common(buf, size,
	    UCL_STRING_RAW|UCL_STRING_TRIM);
	ret = pkg_message_from_ucl(pkg, obj);
	ucl_object_unref(obj);
	free(buf);

	return (ret);
}

int
pkg_load_metadata(struct pkg *pkg, const char *mfile, const char *md_dir,
    const char *plist, const char *rootdir, bool testing)
{
	struct pkg_manifest_key *keys = NULL;
	const char		*arch;
	regex_t			 preg;
	regmatch_t		 pmatch[2];
	int			 i, ret = EPKG_OK;
	int			 mfd = -1;
	size_t			 size;
	bool			 defaultarch = false;

	if (md_dir != NULL &&
	    (mfd = open(md_dir, O_DIRECTORY|O_CLOEXEC)) == -1) {
		pkg_emit_errno("open", md_dir);
		goto cleanup;
	}

	pkg_manifest_keys_new(&keys);

	if (mfile != NULL) {
		ret = pkg_parse_manifest_file(pkg, mfile, keys);
	}

	/* Load the manifest from the metadata directory */
	if (mfile == NULL && mfd != -1 &&
	    (ret = pkg_parse_manifest_fileat(mfd, pkg, "+MANIFEST", keys))
	    != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	/* if no descriptions provided then try to get it from a file */
	if (mfd != -1 && pkg->desc == NULL)
		pkg_load_from_file(mfd, pkg, PKG_DESC, "+DESC");

	/* if no message try to get it from a file */
	if (mfd != -1 && pkg->message == NULL) {
		/* Try ucl version first */
		pkg_load_message_from_file(mfd, pkg, "+DISPLAY");
	}

	/* if no arch autodetermine it */
	if (pkg->abi == NULL) {
#ifdef __FreeBSD__
		char *str_osversion;
		xasprintf(&str_osversion, "%d", osversion);
		pkg_kv_add(&pkg->annotations, "FreeBSD_version", str_osversion, "annotation");
#endif
		arch = pkg_object_string(pkg_config_get("ABI"));
		pkg->abi = xstrdup(arch);
		defaultarch = true;
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
			pkg->www = xstrndup(&pkg->desc[pmatch[1].rm_so], size);
		} else {
			pkg->www = xstrdup("UNKNOWN");
		}
		regfree(&preg);
	}

	if (!testing)
		pkg_analyse_files(NULL, pkg, rootdir);

	if (developer_mode)
		pkg_suggest_arch(pkg, pkg->abi, defaultarch);

cleanup:
	if (mfd != -1)
		close(mfd);
	pkg_manifest_keys_free(keys);
	return (ret);
}

int
pkg_create_staged(const char *outdir, pkg_formats format, const char *rootdir,
    const char *md_dir, char *plist)
{
	struct pkg	*pkg = NULL;
	struct pkg_file	*file = NULL;
	struct pkg_dir	*dir = NULL;
	struct packing	*pkg_archive = NULL;
	int		 ret = EPKG_FATAL;

	pkg_debug(1, "Creating package from stage directory: '%s'", rootdir);

	if ((ret = pkg_new(&pkg, PKG_FILE)) != EPKG_OK)
		goto cleanup;

	if ((ret = pkg_load_metadata(pkg, NULL, md_dir, plist, rootdir, false))
	    != EPKG_OK)
		goto cleanup;

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
	free(pkg);
	packing_finish(pkg_archive);
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

	packing_finish(pkg_archive);

	return (EPKG_OK);
}

static int64_t	count;
static int64_t  maxcount;
static const char *what;

static int magnitude(int64_t num)
{
	int oom;

	if (num == 0)
		return (1);
	if (num < 0)
		num = -num;

	for (oom = 1; num >= 10; oom++)
		num /= 10;

	return (oom);
}

static void
counter_init(const char *count_what, int64_t max)
{
	count = 0;
	what = count_what;
	maxcount = max;
	pkg_emit_progress_start("%-20s%*s[%jd]", what,
	    6 - magnitude(maxcount), " ", (intmax_t)maxcount);

	return;
}

static void
counter_count(void)
{
	count++;

	if (count % TICK == 0)
		pkg_emit_progress_tick(count, maxcount);

	return;
}

static void
counter_end(void)
{
	pkg_emit_progress_tick(count, maxcount);
	return;
}
