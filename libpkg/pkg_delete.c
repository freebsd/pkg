#include <string.h>
#include <err.h>
#include <unistd.h>
#include <sha256.h>
#include <search.h>
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>

#include "pkg.h"
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
	char sha256[65];
	const char *mtree = NULL;
	struct archive *a;
	struct archive_entry *ae;
	struct array mtreedirs;
	const char *prefix;
	char *path, *end, *fullpath;
	int ret;

	if (pkg == NULL || db == NULL)
		return (-1);

	rdeps = pkg_rdeps(pkg);
	files = pkg_files(pkg);
	prefix = pkg_get(pkg, PKG_PREFIX);

	if (rdeps == NULL || files == NULL)
		return (-1);

	if (rdeps[0] != NULL && force == 0) {
		warnx("%s is required by other packages", pkg_get(pkg, PKG_ORIGIN));
		return (-1); /* TODO: special return code */
	}

	a = archive_read_new();
	archive_read_support_compression_none(a);
	archive_read_support_format_mtree(a);

	mtree = pkg_get(pkg, PKG_MTREE);
	if (archive_read_open_memory(a, strdup(mtree), strlen(mtree)) != ARCHIVE_OK)
		return (EPKG_ERROR_MTREE);

	bzero(&mtreedirs, sizeof(mtreedirs));
	array_init(&mtreedirs, 20);

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK)
		array_append(&mtreedirs, strdup(archive_entry_pathname(ae)));

	for (int i = 0; files[i] != NULL; i++) {
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

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}
