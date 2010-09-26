#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <cdb.h>
#include <cJSON.h>

#include "util.h"
#include "pkg_compat.h"
#include "pkgdb_cache.h"

/* open a cdb db */
static int
db_open(struct cdb *db, const char *path, int flags)
{
	int fd;

	if (!path || !db)
		return (-1);

	if ((fd = open(path, flags)) == -1)
		warn("open(%s):", path);
	else
		cdb_init(db, fd);

	return (fd);
}

/* close a cdb db */
static void
db_close(struct cdb *db)
{
	int fd;

	fd = cdb_fileno(db);
	cdb_free(db);
	close(fd);
}

/* query string -> string */
static char *
db_query(struct cdb *db, const char *fmt, ...)
{
	char *string;
	va_list args;
	char key[BUFSIZ];
	size_t len;

	va_start(args, fmt);
	len = vsnprintf(key, sizeof(key), fmt, args);

	if (len != strlen(key)) {
		warnx("key too long:");
		vwarnx(fmt, args);
		va_end(args);
		return NULL;
	}

	va_end(args);

	if (cdb_find(db, key, len) < 0)
		return NULL;

	len = cdb_datalen(db);
	string = malloc(len+1);
	cdb_read(db, string, len, cdb_datapos(db));
	string[len] = '\0';
	return (string);
}

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

	return (cdb_make_add(db, key, len, val, strlen(val)));
}

/* query a pkg using index */
static struct pkg *
pkg_idx_query(struct cdb *db, int idx)
{
	struct pkg *pkg;
	size_t len;

	if (db == NULL)
		return NULL;

	if (cdb_find(db, &idx, sizeof(idx) <= 0))
		return NULL;

	pkg = malloc(sizeof(*pkg));
	len = cdb_datalen(db);
	pkg->name = malloc(len+1);
	cdb_read(db, pkg->name, len, cdb_datapos(db));
	pkg->name[len] = '\0';

	pkg->version = db_query(db, PKGDB_VERSION, idx);
	pkg->comment = db_query(db, PKGDB_COMMENT, idx);
	pkg->desc    = db_query(db, PKGDB_DESC, idx);
	pkg->origin  = db_query(db, PKGDB_ORIGIN, idx);

	return (pkg);
}

/* query pkg using name */
/* static struct pkg *
pkg_query(struct cdb *db, char *name)
{
	int idx;

	if (name == NULL || db == NULL)
		return NULL;

	if ((cdb_find(db, name, strlen(name))) <= 0)
		return NULL;

	cdb_read(db, &idx, sizeof(idx), cdb_datapos(db));

	return (pkg_idx_query(db, idx));
}
*/

/* open the pkgdb.cache */
static int
pkgdb_open(struct cdb *db, int flags)
{
	char *pkgdb_dir;
	char filepath[MAXPATHLEN];

	pkgdb_dir = getenv("PKG_DBDIR");

	snprintf(filepath, sizeof(filepath), "%s/pkgdb.cache",
			(pkgdb_dir) ? pkgdb_dir : PKG_DBDIR);
	return (db_open(db, filepath, flags));
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
	char *value;
	char tmppath[MAXPATHLEN];
	struct cdb_make cdb_make;
	DIR *dir;
	struct dirent *portsdir;
	cJSON *manifest, *node;
	int idx = 0;

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

				value = cJSON_GetObjectItem(manifest, "name")->valuestring;

				/* index -> name */
				cdb_make_add(&cdb_make, &idx, sizeof(idx),
						value, strlen(value));
				/* name -> index */
				cdb_make_add(&cdb_make, value, strlen(value), &idx, sizeof(idx));

				value = cJSON_GetObjectItem(manifest, "version")->valuestring;
				db_add(&cdb_make, value, PKGDB_VERSION, idx);

				if ((node = cJSON_GetObjectItem(manifest, "comment")) != NULL)
					db_add(&cdb_make, node->valuestring, PKGDB_COMMENT, idx);

				if ((node = cJSON_GetObjectItem(manifest, "desc")) != NULL)
					db_add(&cdb_make, node->valuestring, PKGDB_DESC, idx);

				if ((node = cJSON_GetObjectItem(manifest, "origin")) != NULL)
					db_add(&cdb_make, node->valuestring, PKGDB_ORIGIN, idx);

				cJSON_Delete(manifest);

				idx++;
			}
		}
	}

	/* record packages len */
	cdb_make_add(&cdb_make, "nb_packages", strlen("nb_packages"), &idx, sizeof(idx));

	cdb_make_finish(&cdb_make);

	close(fd);
	rename(tmppath, cache_path);
	chmod(cache_path, 0644);
}

void
pkgdb_cache_update()
{
	const char *pkg_dbdir;
	char cache_path[MAXPATHLEN];
	struct stat dir_st, cache_st;
	uid_t uid;

	if ((pkg_dbdir = getenv("PKG_DBDIR")) == NULL)
		pkg_dbdir = PKG_DBDIR;

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

	if (stat(cache_path, &cache_st) == -1) {
		if (errno == ENOENT) {
			pkgdb_cache_rebuild(pkg_dbdir, cache_path);
			return;
		}
		else
			err(EXIT_FAILURE, "%s:", cache_path);
	}

	if ( dir_st.st_mtime > cache_st.st_mtime )
		pkgdb_cache_rebuild(pkg_dbdir, cache_path);
}

struct pkg **
pkgdb_cache_list_packages(const char *pattern)
{
	struct cdb db;
	int nb_pkg, i, j;
	char key[BUFSIZ];
	char *val;
	struct pkg **pkgs;
	size_t len;

	if (pkgdb_open(&db, O_RDONLY) == -1)
		return NULL;

	strcpy(key, "nb_packages");

	if (cdb_find(&db, key, strlen(key)) <= 0) {
		warnx("corrupted database");
		db_close(&db);
	}

	cdb_read(&db, &nb_pkg, sizeof(nb_pkg), cdb_datapos(&db));

	pkgs = calloc(nb_pkg+1, sizeof(*pkgs));

	for (i = 0, j = 0; i < nb_pkg; i++) {
		cdb_find(&db, &i, sizeof(i));
		len = cdb_datalen(&db);
		val = malloc(len+1);
		cdb_read(&db, val, len, cdb_datapos(&db));
		val[len] = '\0';

		if (!pattern || strncmp(val, pattern, strlen(pattern)) == 0) {
			/* ok we find one pkg matching the pattern */
			if ((pkgs[j] = pkg_idx_query(&db, i)) != NULL)
				j++;
		}
	}
	pkgs[j] = NULL;

	db_close(&db);

	return (pkgs);
}
