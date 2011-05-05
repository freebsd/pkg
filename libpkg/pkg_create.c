#include <sys/param.h>

#include <archive.h>
#include <archive_entry.h>
#include <err.h>
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
	char newpath[MAXPATHLEN];
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
				snprintf(fpath, sizeof(MAXPATHLEN), "%s%s", root, newpath);
			else
				strlcpy(fpath, newpath, MAXPATHLEN);

			packing_append_file(pkg_archive, fpath, pkg_file_path(files[i]));
		}
	}

	return (EPKG_OK);
}

int
pkg_create(const char *mpath, pkg_formats format, const char *outdir, const char *rootdir, struct pkg *pkg)
{
	char namever[FILENAME_MAX];
	struct packing *pkg_archive;
	char archive_path[MAXPATHLEN];
	int required_flags = PKG_LOAD_DEPS | PKG_LOAD_CONFLICTS | PKG_LOAD_FILES |
						 PKG_LOAD_EXECS | PKG_LOAD_SCRIPTS | PKG_LOAD_OPTIONS |
						 PKG_LOAD_MTREE;

	(void)mpath;

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	/*
	 * Ensure that we have all the information we need
	 */
	if ((pkg->flags & required_flags) != required_flags)
		return (ERROR_BAD_ARG("pkg"));

	snprintf(namever, sizeof(namever), "%s-%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	snprintf(archive_path, sizeof(archive_path), "%s/%s", outdir, namever);

	if (packing_init(&pkg_archive, archive_path, format) != EPKG_OK) {
		/* TODO do it smarter */
		return pkg_error_set(EPKG_FATAL, "unable to create archive");
	}

	pkg_create_from_dir(pkg, rootdir, pkg_archive);

	return (packing_finish(pkg_archive));

	return (EPKG_OK);
}
