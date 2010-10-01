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

/* add record formated string */
static int
pkgdb_vadd(struct cdb_make *db, const void *val, size_t vallen, const char *fmt, va_list args)
{
	char key[BUFSIZ];
	size_t len;

	if (db == NULL || key == NULL || val == NULL)
		return (-1);

	len = vsnprintf(key, sizeof(key), fmt, args);

	if (len != strlen(key)) {
		warnx("key too long:");
		vwarnx(fmt, args);
		return (-1);
	}

	/* record the last \0 */
	return (cdb_make_add(db, key, len, val, vallen));
}

static int
pkgdb_add(struct cdb_make *db, const void *val, size_t vallen, const char *fmt, ...)
{
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = pkgdb_vadd(db, val, vallen, fmt, args);
	va_end(args);

	return (ret);
}

/* add record formated string -> string (record the last \0 on value) */
static int
pkgdb_add_string(struct cdb_make *db, const char *val, const char *fmt, ...)
{
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = pkgdb_vadd(db, val, strlen(val)+1, fmt, args);
	va_end(args);

	return (ret);
}

static char*cJSON_GetString(cJSON*j,const char *k){/*cJSON style*/cJSON*n;if((n=cJSON_GetObjectItem(j,k)))return n->valuestring;return NULL;}

static struct pkg *
pkg_from_manifest(cJSON *manifest)
{
	cJSON *node, *array;
	struct pkg *pkg;
	char *dep_name;
	char *dep_version;
	int i, array_size, j;

	pkg = calloc(1, sizeof(*pkg));
	pkg->name = cJSON_GetString(manifest, "name");
	pkg->version = cJSON_GetString(manifest, "version");
	pkg->comment = cJSON_GetString(manifest, "comment");
	pkg->origin = cJSON_GetString(manifest, "origin");

	if (pkg->name == NULL || pkg->version == NULL ||
			pkg->comment == NULL || pkg->origin == NULL) {
		free(pkg);
		pkg = NULL;
	}
	else {
		pkg->desc = cJSON_GetString(manifest, "desc");

		array = cJSON_GetObjectItem(manifest, "deps");

		if (array && (array_size = cJSON_GetArraySize(array)) > 0) {

			pkg->deps = calloc(array_size+1, sizeof(struct pkg *));

			for (i = 0, j = 0; i < array_size; i++) {
				if ((node = cJSON_GetArrayItem(array, i)) != NULL) {
					dep_name = cJSON_GetString(node, "name");
					dep_version = cJSON_GetString(node, "version");
					if (dep_name != NULL && dep_version != NULL) {
						pkg->deps[j] = calloc(1, sizeof(struct pkg));
						pkg->deps[j]->name = dep_name;
						pkg->deps[j]->version = dep_version;
						j++;
					}
				}
			}
		}
	}
	return (pkg);
}

static struct pkg *
pkg_from_dir(struct pkgdb *db, const char *pkg_dbdir, char *pkgname)
{
	cJSON *manifest;
	char manifestpath[MAXPATHLEN];
	char *buffer;
	struct stat st;
	struct pkg *pkg = NULL;
	const size_t *idx;
	const time_t *old_mtime = 0;

	snprintf(manifestpath, sizeof(manifestpath), "%s/%s/+MANIFEST", pkg_dbdir,
			pkgname);

	if (stat(manifestpath, &st) == -1) {
		warn("stat(%s):", manifestpath);
		return NULL;
	}

	/* compare with last manifest mtime */
	if (cdb_fileno(&db->db) != -1 &&
			(idx = pkgdb_query(db, "%s", pkgname)) != NULL &&
			(old_mtime = pkgdb_query(db, PKGDB_MTIME, *idx)) != NULL &&
			st.st_mtime == *old_mtime &&
			(pkg = pkgdb_pkg_query(db, *idx)) != NULL) { /* jackpot */
		pkgdb_deps_query(db, pkg);
		pkg->mtime = st.st_mtime;
		return (pkg);
	}

	if ((file_to_buffer(manifestpath, &buffer)) == -1) {

		warnx("An error occured while trying to read "
				"+MANIFEST for %s, falling back to old "
				"+CONTENTS format", pkgname);

		manifest = pkg_compat_convert_installed( pkg_dbdir, pkgname,
				manifestpath);
	}
	else {
		manifest = cJSON_Parse(buffer);
		free(buffer);
	}

	if (manifest == NULL) {
		warnx("%s: Manifest corrputed, skipping", pkgname);
		return NULL;
	}

	if ((pkg = pkg_from_manifest(manifest)) == NULL)
		cJSON_Delete(manifest);
	else
		pkg->mtime = st.st_mtime;

	return (pkg);
}

static void
pkgdb_cache_rebuild(struct pkgdb *db, const char *pkg_dbdir, const char *cache_path)
{
	int fd;
	char tmppath[MAXPATHLEN], namever[FILENAME_MAX];
	struct cdb_make cdb_make;
	DIR *dir;
	struct dirent *portsdir;
	struct pkg *pkg, **deps;
	size_t idx;

	snprintf(tmppath, sizeof(tmppath), "%s/pkgdb.cache-XXXXX", pkg_dbdir);

	if ((fd = mkstemp(tmppath)) == -1)
		return;

	warnx("Rebuilding cache...");

	cdb_make_start(&cdb_make, fd);

	if (pkgdb_open(db) == -1)
		cdb_fileno(&db->db) = -1;

	/* Now go through pkg_dbdir rebuild the cache */

	if ((dir = opendir(pkg_dbdir)) != NULL) {
		while ((portsdir = readdir(dir)) != NULL) {
			if (strcmp(portsdir->d_name, ".") != 0 &&
					strcmp(portsdir->d_name, "..") !=0) {

				if (portsdir->d_type != DT_DIR)
					continue;

				pkg = pkg_from_dir(db, pkg_dbdir, portsdir->d_name);

				if (pkg == NULL)
					continue;

				snprintf(namever, sizeof(namever), "%s-%s", pkg->name, pkg->version);
				/* index -> namever */
				cdb_make_add(&cdb_make, &idx, sizeof(idx), namever, strlen(namever)+1);
				/* namever -> index */
				cdb_make_add(&cdb_make, namever, strlen(namever), &idx, sizeof(idx));
				/* name -> index */
				cdb_make_add(&cdb_make, pkg->name, strlen(pkg->name), &idx, sizeof(idx));
				pkgdb_add_string(&cdb_make, pkg->name, PKGDB_NAME, idx);
				pkgdb_add_string(&cdb_make, pkg->comment, PKGDB_COMMENT, idx);
				pkgdb_add_string(&cdb_make, pkg->origin, PKGDB_ORIGIN, idx);
				pkgdb_add_string(&cdb_make, pkg->desc, PKGDB_DESC, idx);
				pkgdb_add(&cdb_make, &pkg->mtime, sizeof(pkg->mtime), PKGDB_MTIME, idx);

				if (pkg->deps) {
					for (deps = pkg->deps; *deps != NULL; deps++) {
						snprintf(namever, sizeof(namever), "%s-%s", (*deps)->name, (*deps)->version);
						pkgdb_add_string(&cdb_make, namever, PKGDB_DEPS, idx);
						free(*deps);
					}
					free(pkg->deps);
				}

				cJSON_Delete(pkg->manifest);
				free(pkg);
				idx++;
			}
		}
		closedir(dir);
	}

	/* close old db */
	if (cdb_fileno(&db->db) != -1) {
		close(cdb_fileno(&db->db));
		cdb_free(&db->db);
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
		pkgdb_cache_rebuild(db, pkg_dbdir, cache_path);
		pkgdb_unlock(db);
	}
}
