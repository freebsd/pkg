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
dircmp(char *path, struct array *a)
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
	struct array mtreedirs;
	const char *prefix;
	char *path, *end, *fullpath;
	struct sbuf *script_cmd;
	int ret, i;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (db == NULL)
		return (ERROR_BAD_ARG("db"));

	rdeps = pkg_rdeps(pkg);
	files = pkg_files(pkg);
	prefix = pkg_get(pkg, PKG_PREFIX);

	if (rdeps == NULL || files == NULL)
		return (pkg_error_set(EPKG_FATAL, "missing deps and files infos"));

	if (rdeps[0] != NULL && force == 0) {
		warnx("%s is required by other packages", pkg_get(pkg, PKG_ORIGIN));
		return (pkg_error_set(EPKG_REQUIRED, "%s is required by other"
							  "packages", pkg_get(pkg, PKG_NAME)));
	}

	script_cmd = sbuf_new_auto();
	/* execute PRE_DEINSTALL */
	if ((scripts = pkg_scripts(pkg)) != NULL)
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

	bzero(&mtreedirs, sizeof(mtreedirs));
	array_init(&mtreedirs, 20);

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK)
		array_append(&mtreedirs, strdup(archive_entry_pathname(ae)));

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

	if (scripts != NULL)
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
	if ((execs = pkg_execs(pkg)) != NULL)
		for (i = 0; execs[i] != NULL; i++)
			if (pkg_exec_type(execs[i]) == PKG_UNEXEC)
				system(pkg_exec_cmd(execs[i]));

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}
