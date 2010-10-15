#include <sys/param.h>
#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cdb.h>

#include "util.h"
#include "pkgdb.h"
#include "pkg_compat.h"
#include "pkg_manifest.h"
#include "pkgdb_cache.h"

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

	val = pkgdb_cache_vget(pkg->pdb->cdb, attr_key_map[i].key, pkg->idx);
	return (val);
}

int
pkgdb_cache_query(struct pkgdb *db, struct pkg *pkg)
{
	const int32_t *idx;

	/* If we are looking for an exact match, no needs to loop over all entries */
	if (db->match == MATCH_EXACT) {
		if (db->i == 0 && (idx = pkgdb_cache_vget(db->cdb, "%s", db->pattern)) != NULL) {
			pkg->name = db->pattern;
			pkg->idx = *idx;
			pkg->pdb = db;
			db->i++;
			return (0);
		} else
			return (-1);
	}

	while ((pkg->name = pkgdb_cache_vget(db->cdb, PKGDB_NAME, db->i)) != NULL) {
		if (db->match == MATCH_ALL || pkgdb_match(db, pkg->name) == 0) {
			pkg->idx = db->i++;
			pkg->pdb = db;
			return (0);
		}

		db->i++;
	}

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
	const int32_t *idx;
	int ret = -1;

	if ((dep->name = pkgdb_cache_vget(pkg->pdb->cdb, PKGDB_DEPS, pkg->idx, pkg->idep)) != NULL &&
		(idx = pkgdb_cache_vget(pkg->pdb->cdb, "%s", dep->name)) != NULL) {
		dep->idx = *idx;
		dep->pdb = pkg->pdb;
		pkg->idep++;
		ret = 0;
	}
	return (ret);
}

int
pkgdb_cache_rdep(struct pkg *pkg, struct pkg *rdep) {
	struct pkg dep;

	while ((rdep->name = pkgdb_cache_vget(pkg->pdb->cdb, PKGDB_NAME, pkg->irdep)) != NULL) {
		rdep->idx = pkg->irdep++;
		rdep->pdb = pkg->pdb;
		while (pkgdb_cache_dep(rdep, &dep) == 0) {
			if (strcmp(dep.name, pkg->name) == 0)
				return (0);
		}
		rdep->idep = 0;
	}
	return (-1);
}

static int
pkgdb_cache_rebuild(struct pkgdb *db, const char *pkg_dbdir,
					const char *cache_path, time_t cache_mtime)
{
	int fd;
	char tmppath[MAXPATHLEN];
	char mpath[MAXPATHLEN];
	char *hyphen;
	struct dirent **pkg_dirs;
	struct pkgdb old_db;
	struct stat m_st;
	struct cdb_make cdb;
	struct pkg pkg;
	struct pkg dep;
	struct pkg_manifest *m = NULL;
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

	/* If we have an old db, open it now */
	if (cache_mtime > 0 && pkgdb_open2(&old_db, false) == -1)
		errx(EXIT_FAILURE, "can not open old db");

	cdb_make_start(&cdb, fd);

	for (idx = 0; idx < nb_pkg; idx++) {
		snprintf(mpath, sizeof(mpath), "%s/%s/+MANIFEST", pkg_dbdir,
				 pkg_dirs[idx]->d_name);

		if (stat(mpath, &m_st) == -1) {
			if (errno == ENOENT)
				m_st.st_mtime = -1;
			else {
				pkgdb_set_error(db, errno, "stat(%s)", mpath);
				return (-1);
			}
		}

		/* Check if the data in the old cache is up-to-date */
		if (cache_mtime > 0 && m_st.st_mtime > 0 && m_st.st_mtime < cache_mtime) {
			/* Remove the version */
			hyphen = strchr(pkg_dirs[idx]->d_name, '-');
			hyphen[0] = '\0';

			pkgdb_query_init(&old_db, pkg_dirs[idx]->d_name, MATCH_EXACT);
			if (pkgdb_query(&old_db, &pkg) != 0)
				errx(EXIT_FAILURE, "%s not in cache, why?", pkg_dirs[idx]->d_name);
			pkgdb_query_free(&old_db);
		} else {
			/* The +MANIFEST file exists, load it */
			if (m_st.st_mtime != -1 && (m = pkg_manifest_load_file(mpath)) == NULL) {
				warnx("skipping %s", mpath);
				continue;
			}
			/* Create the +MANIFEST file via +CONTENTS */
			if (m_st.st_mtime == -1) {
				m = pkg_compat_convert_installed(pkg_dbdir, pkg_dirs[idx]->d_name, mpath);
				if (m == NULL) {
					warnx("error while converting +CONTENTS to %s, skipping", mpath);
					continue;
				}
			}
			pkg_from_manifest(&pkg, m);
		}

		pkgdb_cache_add_int(&cdb, pkg_name(&pkg), idx);

		pkgdb_cache_add_string(&cdb, pkg_name(&pkg), PKGDB_NAME, idx);
		pkgdb_cache_add_string(&cdb, pkg_version(&pkg), PKGDB_VERSION, idx);
		pkgdb_cache_add_string(&cdb, pkg_comment(&pkg), PKGDB_COMMENT, idx);
		pkgdb_cache_add_string(&cdb, pkg_origin(&pkg), PKGDB_ORIGIN, idx);
		pkgdb_cache_add_string(&cdb, pkg_desc(&pkg), PKGDB_DESC, idx);

		idep = 0;
		while (pkg_dep(&pkg, &dep) == 0) {
			pkgdb_cache_add_string(&cdb, pkg_name(&dep), PKGDB_DEPS, idx, idep);
			idep++;
		}

		if (m != NULL) {
			pkg_manifest_free(m);
			m = NULL;
		}
		free(pkg_dirs[idx]);
	}
	free(pkg_dirs);

	if (cache_mtime > 0)
		pkgdb_close(&old_db);

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

	pkgdb_lock(db, LOCK_SH);
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
		/*
		* Upgrade the lock to an exclusive lock because we may convert some
		* +CONTENTS to +MANIFEST format.
		*/
		pkgdb_lock(db, LOCK_EX);
		ret = pkgdb_cache_rebuild(db, pkg_dbdir, cache_path,
								  (errno != ENOENT) ? cache_st.st_mtime : -1);
	}

	pkgdb_lock(db, LOCK_UN);
	return (ret);
}

int
pkgdb_cache_open(struct pkgdb *db, bool rebuild)
{
	char path[MAXPATHLEN];
	int fd;

	if (rebuild == true && pkgdb_cache_update(db) == -1)
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
pkgdb_cache_close(struct pkgdb *db)
{
	close(cdb_fileno(db->cdb));
	cdb_free(db->cdb);
	free(db->cdb);
	return;
}
