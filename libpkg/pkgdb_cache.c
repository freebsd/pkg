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

/* query string -> string */
static const char *
db_query(struct cdb *db, const char *fmt, ...)
{
	const char *string;
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

	db_get(string, db);
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

	va_end(args);

	/* record the last \0 */
	return (cdb_make_add(db, key, len, val, strlen(val)+1));
}

/* query a pkg using index */
static struct pkg *
pkg_idx_query(struct cdb *db, int idx)
{
	struct pkg *pkg;

	if (db == NULL)
		return NULL;

	if (cdb_find(db, &idx, sizeof(idx)) <= 0)
		return NULL;

	pkg = malloc(sizeof(*pkg));
	TAILQ_INIT(&pkg->deps);
	db_get(pkg->name, db);

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
	int *idx;

	if (name == NULL || db == NULL)
		return NULL;

	if ((cdb_find(db, name, strlen(name))) <= 0)
		return NULL;

	db_get(idx, db);
	return (pkg_idx_query(db, *idx));
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
						value, strlen(value)+1);
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
	cdb_make_add(&cdb_make, PKGDB_COUNT, strlen(PKGDB_COUNT), &idx, sizeof(idx));

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

static int
pkg_cmp(void const *a, void const *b)
{
	struct pkg * const *pa = a;
	struct pkg * const *pb = b;
	return (strcmp((*pa)->name, (*pb)->name));
}

void
pkgdb_cache_init(struct pkgdb *db, const char *pattern)
{
	int count, i;
	struct pkg *pkg;
	struct pkg **pkgs;
	TAILQ_HEAD(, pkg) head;
	char *name;

	TAILQ_INIT(&head);
	TAILQ_INIT(&db->pkgs);
	db->count = 0;

	if (pkgdb_open(&db->db, O_RDONLY) == -1)
		return;

	if (cdb_find(&db->db, PKGDB_COUNT, strlen(PKGDB_COUNT)) <= 0) {
		warnx("corrupted database");
		return;
	}

	cdb_read(&db->db, &count, sizeof(count), cdb_datapos(&db->db));

	for (i = 0; i < count; i++) {
		/* get package */
		if ((pkg = pkg_idx_query(&db->db, i)) == NULL)
			continue;

		if (asprintf(&name, "%s-%s", pkg->name, pkg->version) == -1) {
			warn("asprintf(%s-%s):", pkg->name, pkg->version);
			free(pkg);
			continue;
		}

		if (!pattern || strncmp(name, pattern, strlen(pattern)) == 0) {
			TAILQ_INSERT_TAIL(&head, pkg, entry);
			db->count++;
		}
		else
			free(pkg);

		free(name);
	}

	/* sort packages */
	pkgs = calloc(db->count, sizeof(struct pkg));
	i = 0;
	TAILQ_FOREACH(pkg, &head, entry)
		pkgs[i++] = pkg;

	qsort(pkgs, db->count, sizeof(*pkgs), pkg_cmp);

	for (i = 0; i < (int)db->count; i++)
		TAILQ_INSERT_TAIL(&db->pkgs, pkgs[i], entry);

	free(pkgs);

	return;
}
