#include <sys/param.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <glob.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

static int pkg_create_from_dir(struct pkg *, const char *, struct packing *);

static int
pkg_create_from_dir(struct pkg *pkg, const char *root, struct packing *pkg_archive)
{
	char fpath[MAXPATHLEN + 1];
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	char *m;
	const char *mtree;
	struct stat st;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];

	/*
	 * if the checksum is not provided in the manifest recompute it
	 */
	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (root != NULL)
			snprintf(fpath, sizeof(fpath), "%s%s", root, pkg_file_get(file, PKG_FILE_PATH));
		else
			strlcpy(fpath, pkg_file_get(file, PKG_FILE_PATH), sizeof(fpath));

		if ((pkg_file_get(file, PKG_FILE_SUM) == NULL || pkg_file_get(file, PKG_FILE_SUM)[0] == '\0') && lstat(fpath, &st) == 0 && !S_ISLNK(st.st_mode)) {
			if (sha256_file(fpath, sha256) != EPKG_OK)
				return (EPKG_FATAL);
			strlcpy(file->sum, sha256, sizeof(file->sum));
		}

	}

	pkg_emit_manifest(pkg, &m);
	packing_append_buffer(pkg_archive, m, "+MANIFEST", strlen(m));
	free(m);

	pkg_get(pkg, PKG_MTREE, &mtree);
	if (mtree != NULL)
		packing_append_buffer(pkg_archive, mtree, "+MTREE_DIRS", strlen(mtree));

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (root != NULL)
			snprintf(fpath, sizeof(fpath), "%s%s", root, pkg_file_get(file, PKG_FILE_PATH));
		else
			strlcpy(fpath, pkg_file_get(file, PKG_FILE_PATH), sizeof(fpath));

		packing_append_file_attr(pkg_archive, fpath, pkg_file_get(file, PKG_FILE_PATH), file->uname, file->gname, file->perm);
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (root != NULL)
			snprintf(fpath, sizeof(fpath), "%s%s", root, pkg_dir_path(dir));
		else
			strlcpy(fpath, pkg_dir_path(dir), sizeof(fpath));

		packing_append_file_attr(pkg_archive, fpath, pkg_dir_path(dir), dir->uname, dir->gname, dir->perm);
	}

	return (EPKG_OK);
}

static struct packing *
pkg_create_archive(const char *outdir, struct pkg *pkg, pkg_formats format, int required_flags)
{
	char *pkg_path = NULL;
	struct packing *pkg_archive = NULL;
	const char *pkgname, *pkgversion;

	/*
	 * Ensure that we have all the information we need
	 */
	assert((pkg->flags & required_flags) == required_flags);

	if (mkdirs(outdir) != EPKG_OK)
		return NULL;

	pkg_get(pkg, PKG_NAME, &pkgname, PKG_VERSION, &pkgversion);
	if (asprintf(&pkg_path, "%s/%s-%s", outdir, pkgname, pkgversion) == -1) {
		pkg_emit_errno("asprintf", "");
		return (NULL);
	}

	if (packing_init(&pkg_archive, pkg_path, format) != EPKG_OK)
		pkg_archive = NULL;

	if (pkg_path != NULL)
		free(pkg_path);

	return pkg_archive;
}

int
pkg_create_fakeroot(const char *outdir, pkg_formats format, const char *rootdir, const char *metadatadir)
{
	struct pkg *pkg = NULL;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	struct packing *pkg_archive = NULL;
	char *manifest = NULL, *manifest_path = NULL;
	int ret = ENOMEM;

	/* Load the manifest from the metadata directory */
	if (asprintf(&manifest_path, "%s/+MANIFEST", metadatadir) == -1)
		goto cleanup;

	pkg_new(&pkg, PKG_FILE);
	if (pkg == NULL)
		goto cleanup;

	if ((ret = pkg_load_manifest_file(pkg, manifest_path)) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	/* Create the archive */
	pkg_archive = pkg_create_archive(outdir, pkg, format, 0);
	if (pkg_archive == NULL) {
		ret = EPKG_FATAL; /* XXX do better */
		goto cleanup;
	}

	if (pkg_files(pkg, &file) != EPKG_OK && pkg_dirs(pkg, &dir) != EPKG_OK) {
		/* Now traverse the file directories, adding to the archive */
		packing_append_tree(pkg_archive, metadatadir, NULL);
		packing_append_tree(pkg_archive, rootdir, "/");
	} else {
		pkg_create_from_dir(pkg, rootdir, pkg_archive);
	}

	ret = EPKG_OK;

cleanup:
	if (pkg != NULL)
		free(pkg);
	if (manifest_path != NULL)
		free(manifest_path);
	if (manifest != NULL)
		free(manifest);
	if (ret == EPKG_OK)
		ret = packing_finish(pkg_archive);
	return ret;
}

int
pkg_create_installed(const char *outdir, pkg_formats format, const char *rootdir, struct pkg *pkg)
{
	struct packing *pkg_archive;
	int required_flags = PKG_LOAD_DEPS | PKG_LOAD_FILES | PKG_LOAD_CATEGORIES |
	    PKG_LOAD_DIRS | PKG_LOAD_SCRIPTS | PKG_LOAD_OPTIONS |
	    PKG_LOAD_MTREE | PKG_LOAD_LICENSES;

	assert(pkg->type == PKG_INSTALLED);

	pkg_archive = pkg_create_archive(outdir, pkg, format, required_flags);
	if (pkg_archive == NULL) {
		pkg_emit_error("unable to create archive");
		return (EPKG_FATAL);
	}

	pkg_create_from_dir(pkg, rootdir, pkg_archive);

	return packing_finish(pkg_archive);
}
