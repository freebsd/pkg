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

#include "util.h"
#include "pkgdb.h"
#include "pkg_compat.h"
#include "pkg_manifest.h"
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

/*
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
*/

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

static int
pkgdb_add_int(struct cdb_make *db, const char *key, size_t val)
{
	return cdb_make_add(db, key, strlen(key), &val, sizeof(size_t));
}

static void
pkgdb_cache_rebuild(const char *pkg_dbdir, const char *cache_path)
{
	int fd;
	char tmppath[MAXPATHLEN];
	char mpath[MAXPATHLEN];
	char namever[FILENAME_MAX];
	struct cdb_make cdb;
	DIR *dir;
	struct dirent *portsdir;
	size_t idx;
	size_t idep;
	struct pkg_manifest *m;

	snprintf(tmppath, sizeof(tmppath), "%s/pkgdb.cache-XXXXX", pkg_dbdir);

	if ((fd = mkstemp(tmppath)) == -1)
		return;

	warnx("Rebuilding cache...");

	cdb_make_start(&cdb, fd);

	/* Now go through pkg_dbdir rebuild the cache */
	idx = 0;
	if ((dir = opendir(pkg_dbdir)) != NULL) {
		while ((portsdir = readdir(dir)) != NULL) {
			if (strcmp(portsdir->d_name, ".") != 0 &&
					strcmp(portsdir->d_name, "..") !=0) {

				if (portsdir->d_type != DT_DIR)
					continue;

				snprintf(mpath, sizeof(mpath), "%s/%s/+MANIFEST", pkg_dbdir,
					 	 portsdir->d_name);
				if ((m = pkg_manifest_load_file(mpath)) == NULL) {
					warnx("%s not found, converting old +CONTENTS file", mpath);
					if ((m = pkg_compat_convert_installed(pkg_dbdir, portsdir->d_name, mpath))
						== NULL) {
						warnx("error while converting, skipping");
						continue;
					}
				}

				snprintf(namever, sizeof(namever), "%s-%s", pkg_manifest_value(m, "name"),
						 pkg_manifest_value(m, "version"));

				pkgdb_add_int(&cdb, namever, idx);
				pkgdb_add_int(&cdb, pkg_manifest_value(m, "name"), idx);

				pkgdb_add_string(&cdb, namever, PKGDB_NAMEVER, idx);
				pkgdb_add_string(&cdb, pkg_manifest_value(m, "name"), PKGDB_NAME, idx);
				pkgdb_add_string(&cdb, pkg_manifest_value(m, "version"), PKGDB_VERSION, idx);
				pkgdb_add_string(&cdb, pkg_manifest_value(m, "comment"), PKGDB_COMMENT, idx);
				pkgdb_add_string(&cdb, pkg_manifest_value(m, "origin"), PKGDB_ORIGIN, idx);
				pkgdb_add_string(&cdb, pkg_manifest_value(m, "desc"), PKGDB_DESC, idx);

				idep = 0;
				pkg_manifest_dep_init(m);
				while (pkg_manifest_dep_next(m) == 0) {
					snprintf(namever, sizeof(namever), "%s-%s", pkg_manifest_dep_name(m),
							 pkg_manifest_dep_version(m));
					pkgdb_add_string(&cdb, namever, PKGDB_DEPS, idx, idep);
					idep++;
				}

				pkg_manifest_free(m);
				idx++;
			}
		}
		closedir(dir);
	}

	/* record packages len */
	cdb_make_add(&cdb, PKGDB_COUNT, strlen(PKGDB_COUNT), &idx, sizeof(idx));
	cdb_make_finish(&cdb);
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
