#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cdb.h>
#include <cJSON.h>

#include "util.h"
#include "pkgdb.h"
#include "pkg_compat.h"
#include "pkgdb_cache.h"


/* add record formated string -> string */
static int
db_add(struct cdb_make *db, const char *val, const char *fmt, ...)
{
	char key[BUFSIZ];
	va_list args;
	size_t len;

	if (db == NULL || key == NULL || val == NULL)
		return (-1);

	va_start(args, fmt);

	len = vsnprintf(key, sizeof(key), fmt, args);

	if (len != strlen(key)) {
		warnx("key too long:");
		vwarnx(fmt, args);
		va_end(args);
		return (-1);
	}

	va_end(args);

	/* record the last \0 */
	return (cdb_make_add(db, key, len, val, strlen(val)+1));
}

static cJSON *
pkgdb_cache_load_port(const char *pkg_dbdir, char *pkgname)
{
	cJSON *manifest;
	char manifestpath[MAXPATHLEN];
	char *buffer;

	snprintf(manifestpath, sizeof(manifestpath), "%s/%s/+MANIFEST", pkg_dbdir,
			 pkgname);

	if ((file_to_buffer(manifestpath, &buffer)) == -1) {
		warnx("An error occured while trying to read "
				"+MANIFEST for %s, falling back to old "
				"+CONTENTS format", pkgname);
		manifest = pkg_compat_convert_installed( pkg_dbdir, pkgname,
				manifestpath);

		return (manifest);
	}

	if ((manifest = cJSON_Parse(buffer)) == 0)
		warnx("%s: Manifest corrputed, skipping", pkgname);

	free(buffer);

	return (manifest);
}

static void
pkgdb_cache_rebuild(const char *pkg_dbdir, const char *cache_path)
{
	int fd;
	char tmppath[MAXPATHLEN], name_version[FILENAME_MAX];
	struct cdb_make cdb_make;
	DIR *dir;
	struct dirent *portsdir;
	cJSON *manifest, *node, *subnode, *array;
	size_t idx = 0, array_size, i;
	struct pkg pkg;

	snprintf(tmppath, sizeof(tmppath), "%s/pkgdb.cache-XXXXX", pkg_dbdir);

	if ((fd = mkstemp(tmppath)) == -1)
		return;

	warnx("Rebuilding cache...");

	cdb_make_start(&cdb_make, fd);

	/* Now go through pkg_dbdir rebuild the cache */

	if ((dir = opendir(pkg_dbdir)) != NULL) {
		while ((portsdir = readdir(dir)) != NULL) {
			if (strcmp(portsdir->d_name, ".") != 0 &&
					strcmp(portsdir->d_name, "..") !=0) {

				if (portsdir->d_type != DT_DIR)
					continue;

				manifest = pkgdb_cache_load_port(pkg_dbdir,
						portsdir->d_name);

				if (manifest == 0)
					continue;

				if ((node = cJSON_GetObjectItem(manifest, "name")) == NULL || node->valuestring == NULL)
					continue;
				pkg.name = node->valuestring;

				if ((node = cJSON_GetObjectItem(manifest, "version")) == NULL || node->valuestring == NULL)
					continue;
				pkg.version = node->valuestring;

				if ((node = cJSON_GetObjectItem(manifest, "comment")) == NULL || node->valuestring == NULL)
					continue;
				pkg.comment = node->valuestring;

				if ((node = cJSON_GetObjectItem(manifest, "origin")) == NULL || node->valuestring == NULL)
					continue;
				pkg.origin = node->valuestring;

				/* index -> name */
				cdb_make_add(&cdb_make, &idx, sizeof(idx), pkg.name, strlen(pkg.name)+1);
				/* name -> index */
				cdb_make_add(&cdb_make, pkg.name, strlen(pkg.name), &idx, sizeof(idx));

				snprintf(name_version, FILENAME_MAX, "%s-%s", pkg.name, pkg.version);

				db_add(&cdb_make, name_version, PKGDB_NAMEVER, idx);
				db_add(&cdb_make, pkg.comment, PKGDB_COMMENT, idx);
				db_add(&cdb_make, pkg.origin, PKGDB_ORIGIN, idx);

				if ((node = cJSON_GetObjectItem(manifest, "desc")) != NULL && node->valuestring != NULL)
					db_add(&cdb_make, node->valuestring, PKGDB_DESC, idx);

				array = cJSON_GetObjectItem(manifest, "deps");

				if (array && (array_size = cJSON_GetArraySize(array)) > 0) {
					for (i = 0; i < array_size; i++) {
						if ((node = cJSON_GetArrayItem(array, i)) != NULL) {

							if ((subnode = cJSON_GetObjectItem(node, "name")) == NULL || subnode->valuestring == NULL)
								continue;
							pkg.name = subnode->valuestring;

							if ((subnode = cJSON_GetObjectItem(node, "version")) == NULL && subnode->valuestring == NULL)
								continue;
							pkg.version = subnode->valuestring;

							snprintf(name_version, FILENAME_MAX, "%s-%s", pkg.name, pkg.version);
							db_add(&cdb_make, name_version, PKGDB_DEPS, idx);
						}
					}
				}

				cJSON_Delete(manifest);
				idx++;
			}
		}
		closedir(dir);
	}

	/* record packages len */
	cdb_make_add(&cdb_make, PKGDB_COUNT, strlen(PKGDB_COUNT), &idx, sizeof(idx));

	cdb_make_finish(&cdb_make);

	close(fd);
	rename(tmppath, cache_path);
	chmod(cache_path, 0644);
}

void
pkgdb_cache_update(struct pkgdb *db)
{
	const char *pkg_dbdir;
	char cache_path[MAXPATHLEN];
	struct stat dir_st, cache_st;
	uid_t uid;

	pkg_dbdir = pkgdb_get_dir();
	uid = getuid();

	if (stat(pkg_dbdir, &dir_st) == -1) {
		if (uid != 0)
			err(EXIT_FAILURE, "%s:", pkg_dbdir);

		if (errno == ENOENT)
			return;
		else
			err(EXIT_FAILURE, "%s:", pkg_dbdir);
	}

	snprintf(cache_path, sizeof(cache_path), "%s/pkgdb.cache", pkg_dbdir);

	errno = 0; /* Reset it in case it is set to ENOENT */
	if (stat(cache_path, &cache_st) == -1 && errno != ENOENT)
		err(EXIT_FAILURE, "%s:", cache_path);

	if (errno == ENOENT || dir_st.st_mtime > cache_st.st_mtime) {
		pkgdb_lock(db, 1);
		pkgdb_cache_rebuild(pkg_dbdir, cache_path);
		pkgdb_unlock(db);
	}
}
