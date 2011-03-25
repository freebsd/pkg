#include <string.h>
#include <err.h>
#include <unistd.h>
#include <sha256.h>
#include <search.h>
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_util.h"

static
int
dircmp(char const * const path, struct array *a)
{
	for (size_t i = 0; i < a->len; i++)
		if (strcmp(path, a->data[i]) == 0)
			return (1);

	return (0);
}

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, int force)
{
	struct pkg **rdeps;
	struct pkg_file **files;
	struct pkg_exec **execs;
	struct pkg_script **scripts;
	char sha256[65];
	const char *mtree = NULL;
	struct archive *a;
	struct archive_entry *ae;
	struct array mtreedirs = ARRAY_INIT;
	const char *prefix;
	char *path, *end, *fullpath;
	struct sbuf *script_cmd;
	int ret, i;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (db == NULL)
		return (ERROR_BAD_ARG("db"));

	/*
	 * Ensure that we have all the informations we need
	 */
	if ((ret = pkgdb_loadrdeps(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadfiles(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadscripts(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadexecs(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadmtree(db, pkg)) != EPKG_OK)
		return (ret);

	rdeps = pkg_rdeps(pkg);
	files = pkg_files(pkg);
	scripts = pkg_scripts(pkg);
	execs = pkg_execs(pkg);
	prefix = pkg_get(pkg, PKG_PREFIX);

	if (rdeps[0] != NULL && force == 0)
		return (pkg_error_set(EPKG_REQUIRED, "this package is required by "
							  "other packages"));

	script_cmd = sbuf_new_auto();
	/* execute PRE_DEINSTALL */
	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_DEINSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "set -- %s-%s DEINSTALL\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_PRE_DEINSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "set -- %s-%s\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			default:
				/* just ignore */
				break;
		}
	}

	a = archive_read_new();
	archive_read_support_compression_none(a);
	archive_read_support_format_mtree(a);

	mtree = pkg_get(pkg, PKG_MTREE);
	if (archive_read_open_memory(a, strdup(mtree), strlen(mtree)) != ARCHIVE_OK)
		return (pkg_error_set(EPKG_FATAL, "mtree: %s", archive_error_string(a)));

	array_init(&mtreedirs, 20);

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK)
		array_append(&mtreedirs, strdup(archive_entry_pathname(ae)));

	if (ret != ARCHIVE_EOF) {
		array_free(&mtreedirs, &free);
		return (pkg_error_set(EPKG_FATAL, "%s", archive_error_string(a)));
	}

	for (i = 0; files[i] != NULL; i++) {
		/* check sha256 */
		if (pkg_file_sha256(files[i])[0] != '\0' &&
			(SHA256_File(pkg_file_path(files[i]), sha256) == NULL ||
			strcmp(sha256, pkg_file_sha256(files[i])) != 0))
			warnx("%s fails original SHA256 checksum, not removed",
					pkg_file_path(files[i]));

		else if (unlink(pkg_file_path(files[i])) == -1) {
			warn("unlink(%s)", pkg_file_path(files[i]));
			continue;
		} else {
			/* only delete directories that are in prefix */
			if (STARTS_WITH(pkg_file_path(files[i]), prefix)) {
				path = strdup(pkg_file_path(files[i]));
				fullpath = path;
				path += strlen(prefix) + 1;

				if (path[0] == '/')
					path++;

				while ((end = strrchr(path, '/')) != NULL) {
					end[0] = '\0';
					if (!dircmp(path, &mtreedirs))
						rmdir(fullpath);
				}
				free(fullpath);
			}
		}
	}
	array_free(&mtreedirs, &free);

	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_DEINSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "set -- %s-%s POST-DEINSTALL\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_POST_DEINSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "set -- %s-%s\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			default:
				/* just ignore */
				break;
		}
	}

	sbuf_free(script_cmd);

	/* run the @unexec */
	for (i = 0; execs[i] != NULL; i++)
		if (pkg_exec_type(execs[i]) == PKG_UNEXEC)
			system(pkg_exec_cmd(execs[i]));

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}
