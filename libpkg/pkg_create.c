#include <sys/param.h>

#include <archive.h>
#include <archive_entry.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <glob.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"

static int pkg_create_from_dir(struct pkg *, const char *, struct packing *);

static int
pkg_create_from_dir(struct pkg *pkg, const char *root, struct packing *pkg_archive)
{
	char fpath[MAXPATHLEN];
	struct pkg_file **files;
	struct pkg_script **scripts;
	char *m;
	int i;
	const char *scriptname = NULL;

	pkg_emit_manifest(pkg, &m);

	packing_append_buffer(pkg_archive, m, "+MANIFEST", strlen(m));

	free(m);

	packing_append_buffer(pkg_archive, pkg_get(pkg, PKG_DESC), "+DESC", strlen(pkg_get(pkg, PKG_DESC)));
	packing_append_buffer(pkg_archive, pkg_get(pkg, PKG_MTREE), "+MTREE_DIRS", strlen(pkg_get(pkg, PKG_MTREE)));

	if ((scripts = pkg_scripts(pkg)) != NULL) {
		for (i = 0; scripts[i] != NULL; i++) {
			switch (pkg_script_type(scripts[i])) {
				case PKG_SCRIPT_PRE_INSTALL:
					scriptname = "+PRE_INSTALL";
					break;
				case PKG_SCRIPT_POST_INSTALL:
					scriptname = "+POST_INSTALL";
					break;
				case PKG_SCRIPT_INSTALL:
					scriptname = "+INSTALL";
					break;
				case PKG_SCRIPT_PRE_DEINSTALL:
					scriptname = "+PRE_DEINSTALL";
					break;
				case PKG_SCRIPT_POST_DEINSTALL:
					scriptname = "+POST_DEINSTALL";
					break;
				case PKG_SCRIPT_DEINSTALL:
					scriptname = "+DEINSTALL";
					break;
				case PKG_SCRIPT_PRE_UPGRADE:
					scriptname = "+PRE_UPGRADE";
					break;
				case PKG_SCRIPT_POST_UPGRADE:
					scriptname = "+POST_UPGRADE";
					break;
				case PKG_SCRIPT_UPGRADE:
					scriptname = "+UPGRADE";
					break;
			}
			packing_append_buffer(pkg_archive, pkg_script_data(scripts[i]), scriptname, strlen(pkg_script_data(scripts[i])));
		}
	}

	if ((files = pkg_files(pkg)) != NULL) {
		for (i = 0; files[i] != NULL; i++) {

			if (root != NULL)
				snprintf(fpath, sizeof(MAXPATHLEN), "%s%s", root, pkg_file_path(files[i]));
			else
				strlcpy(fpath, pkg_file_path(files[i]), MAXPATHLEN);

			packing_append_file(pkg_archive, fpath, pkg_file_path(files[i]));
		}
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
	size_t sz;

	/* Load the manifest from the metadata directory */
	if (asprintf(&manifest_path, "%s/+MANIFEST", metadatadir) == -1)
		goto cleanup;
	pkg_new(&pkg);
	pkg->type = PKG_FILE;
	if (pkg == NULL)
		goto cleanup;
	ret = file_to_buffer(manifest_path, &manifest, &sz);
	if (ret != EPKG_OK)
		goto cleanup;
	ret = pkg_parse_manifest(pkg, manifest);

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
						 PKG_LOAD_EXECS | PKG_LOAD_SCRIPTS | PKG_LOAD_OPTIONS |
						 PKG_LOAD_MTREE;

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	pkg_archive = pkg_create_archive(outdir, pkg, format, required_flags);
	if (pkg_archive == NULL)
		return pkg_error_set(EPKG_FATAL, "unable to create archive"); /* XXX do better */

	pkg_create_from_dir(pkg, rootdir, pkg_archive);

	return packing_finish(pkg_archive);
}
