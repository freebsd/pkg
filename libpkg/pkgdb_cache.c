#include <sys/param.h>
#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <cdb.h>

#include "util.h"
#include "pkgdb.h"
#include "pkg_compat.h"
#include "pkg_manifest.h"
#include "pkgdb_cache.h"

#define PKGDB_NAMEVER	"%" PRId32 "nv"
#define PKGDB_NAME		"%" PRId32 "n"
#define PKGDB_VERSION	"%" PRId32 "v"
#define PKGDB_COMMENT	"%" PRId32 "c"
#define PKGDB_DESC		"%" PRId32 "d"
#define PKGDB_ORIGIN	"%" PRId32 "o"
#define PKGDB_DEPS		"%" PRId32 "D%" PRId32
#define PKGDB_COUNT		"count"

static const void *pkgdb_cache_vget(struct cdb *, const char *, ...);
static int pkgdb_cache_vadd(struct cdb_make *, const void *, size_t, const char *, va_list);
static int pkgdb_cache_add_string(struct cdb_make *, const char *, const char *, ...);
static int pkgdb_cache_add_int(struct cdb_make *, const char *, int32_t);
static int pkgdb_cache_update(struct pkgdb *);

const char *
pkgdb_cache_getattr(struct pkg *pkg, const char *attr)
{
	size_t i, len;
	const char *val;
	struct {
		const char *attr;
		const char *key;
	} attr_key_map[] = {
		{"namever", PKGDB_NAMEVER},
		{"name",	PKGDB_NAME},
		{"version",	PKGDB_VERSION},
		{"comment", PKGDB_COMMENT},
		{"desc",	PKGDB_DESC},
		{"origin",	PKGDB_ORIGIN},
	};

	len = sizeof(attr_key_map) / sizeof(attr_key_map[0]);

	for (i = 0; i < len; i++)
		if (strcmp(attr_key_map[i].attr, attr) == 0)
			break;

	pkgdb_lock(pkg->pdb, LOCK_SH);
	val = pkgdb_cache_vget(pkg->pdb->cdb, attr_key_map[i].key, pkg->idx);
	pkgdb_lock(pkg->pdb, LOCK_UN);
	return (val);
}


int
pkgdb_cache_query(struct pkgdb *db, struct pkg *pkg)
{
	pkgdb_lock(db, LOCK_SH);

	while ((pkg->namever = pkgdb_cache_vget(db->cdb, PKGDB_NAMEVER, db->i)) != NULL) {
		if (pkgdb_match(db, pkg->namever) == 0) {
			pkg->idx = db->i++;
			pkg->pdb = db;
			pkgdb_lock(db, LOCK_UN);
			return (0);
		}

		db->i++;
	}
	pkgdb_lock(db, LOCK_UN);

	return (-1);
}

/* add record formated string */
static int
pkgdb_cache_vadd(struct cdb_make *db, const void *val, size_t vallen, const char *fmt, va_list args)
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

/* add record formated string -> string (record the last \0 on value) */
static int
pkgdb_cache_add_string(struct cdb_make *db, const char *val, const char *fmt, ...)
{
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = pkgdb_cache_vadd(db, val, strlen(val)+1, fmt, args);
	va_end(args);

	return (ret);
}

static int
pkgdb_cache_add_int(struct cdb_make *db, const char *key, int32_t val)
{
	return cdb_make_add(db, key, strlen(key), &val, sizeof(int32_t));
}

static const void *
pkgdb_cache_vget(struct cdb *db, const char *fmt, ...)
{
	va_list args;
	char key[BUFSIZ];
	size_t len;
	const void *val;

	va_start(args, fmt);
	len = vsnprintf(key, sizeof(key), fmt, args);

	if (len != strlen(key)) {
		warnx("key too long:");
		vwarnx(fmt, args);
		va_end(args);
		return NULL;
	}

	va_end(args);

	if (cdb_find(db, key, len) <= 0)
		return NULL;

	val = cdb_get(db, cdb_datalen(db), cdb_datapos(db));
	return (val);
}

int
pkgdb_cache_dep(struct pkg *pkg, struct pkg *dep)
{
	const size_t *idx;
	int ret = -1;

	pkg_reset(dep);

	if ((dep->namever = pkgdb_cache_vget(pkg->pdb->cdb, PKGDB_DEPS, pkg->idx, pkg->idep)) != NULL &&
		(idx = pkgdb_cache_vget(pkg->pdb->cdb, "%s", dep->namever)) != NULL) {
		dep->idx = *idx;
		dep->pdb = pkg->pdb;
		pkg->idep++;
		ret = 0;
	}
	return (ret);
}

static int
pkgdb_cache_rebuild(struct pkgdb *db, const char *pkg_dbdir, const char *cache_path)
{
	int fd;
	char tmppath[MAXPATHLEN];
	char mpath[MAXPATHLEN];
	char namever[FILENAME_MAX];
	struct dirent **pkg_dirs;
	struct cdb_make cdb;
	struct pkg_manifest *m;
	int32_t nb_pkg;
	int32_t idx;
	int32_t idep;

	warnx("Rebuilding cache...");

	if ((nb_pkg = scandir(pkg_dbdir, &pkg_dirs, select_dir, alphasort)) == -1) {
		pkgdb_set_error(db, errno, "scandir(%s)", pkg_dbdir);
		return (-1);
	}

	snprintf(tmppath, sizeof(tmppath), "%s/pkgdb.cache-XXXXX", pkg_dbdir);
	if ((fd = mkstemp(tmppath)) == -1) {
		pkgdb_set_error(db, errno, "mkstemp(%s)", tmppath);
		for (idx = 0; idx < nb_pkg; idx++)
			free(pkg_dirs[idx]);
		free(pkg_dirs);
		return (-1);
	}

	cdb_make_start(&cdb, fd);

	for (idx = 0; idx < nb_pkg; idx++) {
		snprintf(mpath, sizeof(mpath), "%s/%s/+MANIFEST", pkg_dbdir,
				 pkg_dirs[idx]->d_name);

		if ((m = pkg_manifest_load_file(mpath)) == NULL) {
			if ((m = pkg_compat_convert_installed(pkg_dbdir, pkg_dirs[idx]->d_name,
				 mpath)) == NULL) {
				warnx("error while inserting %s in cache, skipping", mpath);
				continue;
			}
		}

		snprintf(namever, sizeof(namever), "%s-%s", pkg_manifest_value(m, "name"),
				 pkg_manifest_value(m, "version"));

		pkgdb_cache_add_int(&cdb, namever, idx);
		pkgdb_cache_add_int(&cdb, pkg_manifest_value(m, "name"), idx);

		pkgdb_cache_add_string(&cdb, namever, PKGDB_NAMEVER, idx);
		pkgdb_cache_add_string(&cdb, pkg_manifest_value(m, "name"), PKGDB_NAME, idx);
		pkgdb_cache_add_string(&cdb, pkg_manifest_value(m, "version"), PKGDB_VERSION, idx);
		pkgdb_cache_add_string(&cdb, pkg_manifest_value(m, "comment"), PKGDB_COMMENT, idx);
		pkgdb_cache_add_string(&cdb, pkg_manifest_value(m, "origin"), PKGDB_ORIGIN, idx);
		pkgdb_cache_add_string(&cdb, pkg_manifest_value(m, "desc"), PKGDB_DESC, idx);

		idep = 0;
		pkg_manifest_dep_init(m);
		while (pkg_manifest_dep_next(m) == 0) {
			snprintf(namever, sizeof(namever), "%s-%s", pkg_manifest_dep_name(m),
					 pkg_manifest_dep_version(m));
			pkgdb_cache_add_string(&cdb, namever, PKGDB_DEPS, idx, idep);
			idep++;
		}

		pkg_manifest_free(m);
		free(pkg_dirs[idx]);
	}
	free(pkg_dirs);

	/* record packages len */
	cdb_make_add(&cdb, PKGDB_COUNT, strlen(PKGDB_COUNT), &nb_pkg, sizeof(int32_t));
	cdb_make_finish(&cdb);
	close(fd);
	rename(tmppath, cache_path);
	chmod(cache_path, 0644);
	return (0);
}

static int
pkgdb_cache_update(struct pkgdb *db)
{
	const char *pkg_dbdir;
	char cache_path[MAXPATHLEN];
	struct stat dir_st, cache_st;
	int ret = 0;

	pkg_dbdir = pkgdb_get_dir();

	if (stat(pkg_dbdir, &dir_st) == -1) {
		pkgdb_set_error(db, errno, "stat(%s)", pkg_dbdir);
		return (-1);
	}

	snprintf(cache_path, sizeof(cache_path), "%s/pkgdb.cache", pkg_dbdir);

	errno = 0; /* Reset it in case it is set to ENOENT */
	if (stat(cache_path, &cache_st) == -1 && errno != ENOENT) {
		pkgdb_set_error(db, errno, "stat(%s)", cache_path);
		return (-1);
	}

	if (errno == ENOENT || dir_st.st_mtime > cache_st.st_mtime) {
		pkgdb_lock(db, LOCK_EX);
		ret = pkgdb_cache_rebuild(db, pkg_dbdir, cache_path);
		pkgdb_lock(db, LOCK_UN);
	}

	return (ret);
}

int
pkgdb_cache_init(struct pkgdb *db)
{
	char path[MAXPATHLEN];
	int fd;

	if (pkgdb_cache_update(db) == -1)
		return (-1);

	if ((db->cdb = malloc(sizeof(struct cdb))) == NULL)
		err(EXIT_FAILURE, "malloc()");

	snprintf(path, sizeof(path), "%s/pkgdb.cache", pkgdb_get_dir());

	if ((fd = open(path, O_RDONLY)) < 0) {
		free(db->cdb);
		pkgdb_set_error(db, errno, "open(%s)", path);
		return (-1);
	}

	if (cdb_init(db->cdb, fd) < 0) {
		free(db->cdb);
		pkgdb_set_error(db, errno, "cdb_init on %s", path);
		return (-1);
	}

	return (0);
}

void
pkgdb_cache_free(struct pkgdb *db)
{
	close(cdb_fileno(db->cdb));
	cdb_free(db->cdb);
	free(db->cdb);
	return;
}
