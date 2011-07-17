#include <sys/param.h>

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
	char fpath[MAXPATHLEN];
	struct pkg_file *file = NULL;
	char *m;
	const char *mtree;

	pkg_emit_manifest(pkg, &m);
	packing_append_buffer(pkg_archive, m, "+MANIFEST", strlen(m));
	free(m);

	mtree = pkg_get(pkg, PKG_MTREE);
	if (mtree != NULL)
		packing_append_buffer(pkg_archive, mtree, "+MTREE_DIRS", strlen(mtree));

	while (pkg_files(pkg, &file) == EPKG_OK) {

		if (root != NULL)
			snprintf(fpath, MAXPATHLEN, "%s%s", root, pkg_file_path(file));
		else
			strlcpy(fpath, pkg_file_path(file), MAXPATHLEN);

		packing_append_file(pkg_archive, fpath, pkg_file_path(file));
	}

	return (EPKG_OK);
}

static struct packing *
pkg_create_archive(const char *outdir, struct pkg *pkg, pkg_formats format, int required_flags)
{
	char *pkg_path = NULL;
	struct packing *pkg_archive = NULL;

	/*
	 * Ensure that we have all the information we need
	 */
	if ((pkg->flags & required_flags) != required_flags) {
		printf("error: required flags not set\n");
		return NULL;
	}

	if (asprintf(&pkg_path, "%s/%s-%s", outdir, pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION)) == -1) {
		perror("asprintf");
		return NULL; /* XXX do better */
	}
	if (packing_init(&pkg_archive, pkg_path, format) != EPKG_OK) {
		perror("packing_init");
		return NULL;
	}

	if (pkg_path != NULL)
		free(pkg_path);
	return pkg_archive;
}

int
pkg_create_fakeroot(const char *outdir, pkg_formats format, const char *rootdir, const char *metadatadir)
{
	struct pkg *pkg = NULL;
	struct packing *pkg_archive = NULL;
	char *manifest = NULL, *manifest_path = NULL;
	int ret = ENOMEM;

	/* Load the manifest from the metadata directory */
	if (asprintf(&manifest_path, "%s/+MANIFEST", metadatadir) == -1)
		goto cleanup;

	pkg_new(&pkg, PKG_FILE);
	if (pkg == NULL)
		goto cleanup;

	ret = pkg_load_manifest_file(pkg, manifest_path);

	/* Create the archive */
	pkg_archive = pkg_create_archive(outdir, pkg, format, 0);
	if (pkg_archive == NULL) {
		ret = EPKG_FATAL; /* XXX do better */
		goto cleanup;
	}

	/* Now traverse the file directories, adding to the archive */
	packing_append_tree(pkg_archive, metadatadir, NULL);
	packing_append_tree(pkg_archive, rootdir, "/");
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
	int required_flags = PKG_LOAD_DEPS | PKG_LOAD_CONFLICTS | PKG_LOAD_FILES |
						 PKG_LOAD_DIRS | PKG_LOAD_SCRIPTS | PKG_LOAD_OPTIONS |
						 PKG_LOAD_MTREE;

	assert(pkg->type == PKG_INSTALLED);

	pkg_archive = pkg_create_archive(outdir, pkg, format, required_flags);
	if (pkg_archive == NULL) {
		EMIT_PKG_ERROR("%s", "unable to create archive");
		return (EPKG_FATAL);
	}

	pkg_create_from_dir(pkg, rootdir, pkg_archive);

	return packing_finish(pkg_archive);
}
