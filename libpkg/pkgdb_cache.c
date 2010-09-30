#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fnmatch.h>
#include <regex.h>
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
pkg_idx_query(struct cdb *db, size_t idx)
{
	struct pkg *pkg;

	if (db == NULL)
		return NULL;

	if (cdb_find(db, &idx, sizeof(idx)) <= 0)
		return NULL;

	pkg = malloc(sizeof(*pkg));
	pkg->idx = idx;
	db_get(pkg->name, db);

	pkg->version = db_query(db, PKGDB_VERSION, idx);
	pkg->comment = db_query(db, PKGDB_COMMENT, idx);
	pkg->desc    = db_query(db, PKGDB_DESC, idx);
	pkg->origin  = db_query(db, PKGDB_ORIGIN, idx);
	snprintf(pkg->name_version, FILENAME_MAX, "%s-%s", pkg->name, pkg->version);

	return (pkg);
}

/* populate deps on pkg */
static void
pkg_get_deps(struct cdb *db, struct pkg *pkg)
{
	struct cdb_find cdbf;
	size_t count = 0, idx, j, klen;
	const char *name_version;
	char *name, *version;
	char key[BUFSIZ];
	struct pkg *dep;

	if (db == NULL || pkg == NULL)
		return;

	snprintf(key, BUFSIZ, PKGDB_DEPS, pkg->idx);
	klen = strlen(key);

	cdb_findinit(&cdbf, db, key, klen);

	while (cdb_findnext(&cdbf) > 0)
		count++;

	pkg->deps = calloc(count+1, sizeof(*pkg->deps));
	pkg->deps[count] = NULL;

	cdb_findinit(&cdbf, db, key, klen);

	j = 0;
	while (cdb_findnext(&cdbf) > 0) {
		db_get(name_version, db);
		name = strdup(name_version);

		if ((version = strrchr(name, '-')) == NULL) {
			free(name);
			continue;
		}

		*(version++) = '\0';

		/* get index */
		if (cdb_find(db, name, strlen(name)) <= 0) {
			free(name);
			continue;
		}
		cdb_read(db, &idx, sizeof(idx), cdb_datapos(db));

		/* get package */
		if ((dep = pkg_idx_query(db, idx)) == NULL) {
			/* partial package */
			dep = calloc(1, sizeof(*dep));
			strncpy(dep->name_version, name_version, FILENAME_MAX);
			dep->errors |= PKGERR_NOT_INSTALLED;
		} else { /* package exist */
			if (strcmp(version, dep->version) != 0)
				dep->errors |= PKGERR_VERSION_MISMATCH;
			/* pkg_get_deps(db, dep); */ /* dont need to be recursive for the moment */
		}
		pkg->deps[j++] = dep;
	}
}

/* populate rdeps on package */
static void
pkg_get_rdeps(struct cdb *db, struct pkg *pkg, size_t count)
{
	size_t i, j;
	struct pkg *p, **deps;

	if (db == NULL || pkg == NULL)
		return;

	pkg->rdeps = calloc(count+1, sizeof(struct pkg *));

	for (i = 0, j = 0; i < count; i++) {

		if ((p = pkg_idx_query(db, i)) == NULL)
			continue;

		pkg_get_deps(db, p);

		for (deps = p->deps; *deps != NULL; deps++) {
			if (!((*deps)->errors & PKGERR_NOT_INSTALLED)
					&& strcmp((*deps)->name, pkg->name) == 0) {
				pkg->rdeps[j] = p;
				break;
			}
		}

		/* free deps */
		for (deps = p->deps; *deps != NULL; deps++)
			free(*deps);
		free(p->deps);

		if (pkg->rdeps[j] == p)
			j++;
		else
			free(p);
	}
	pkg->rdeps = realloc(pkg->rdeps, (j+1) * sizeof(struct pkg *));
	pkg->rdeps[j] = NULL;
	return;
}

/* open the pkgdb.cache */
static int
pkgdb_open(struct cdb *db, int flags)
{
	char filepath[MAXPATHLEN];

	snprintf(filepath, sizeof(filepath), "%s/pkgdb.cache", pkgdb_get_dir());

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
	char tmppath[MAXPATHLEN];
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

				db_add(&cdb_make, pkg.version, PKGDB_VERSION, idx);
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

							snprintf(pkg.name_version, FILENAME_MAX, "%s-%s", pkg.name, pkg.version);
							db_add(&cdb_make, pkg.name_version, PKGDB_DEPS, idx);
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
pkgdb_cache_update()
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

static int
pkg_match(struct pkg *pkg, const regex_t *re, const char *pattern, match_t match)
{
	int matched = 1;
	switch (match) {
		case MATCH_ALL:
			matched = 0;
			break;
		case MATCH_EXACT:
			matched = strcmp(pkg->name_version, pattern);
			break;
		case MATCH_GLOB:
			matched = fnmatch(pattern, pkg->name_version, 0);
			break;
		case MATCH_REGEX:
		case MATCH_EREGEX:
			matched = regexec(re, pkg->name_version, 0, NULL, 0);
			break;
	}
	return (matched);
}

void
pkgdb_cache_init(struct pkgdb *db, const char *pattern, match_t match, unsigned char flags)
{
	size_t count, i;
	struct pkg *pkg;
	regex_t re;

	db->count = 0;
	db->flags = flags;

	if (match != MATCH_ALL && pattern == NULL) {
		warnx("a pattern is required");
		return;
	}

	if (pkgdb_open(&db->db, O_RDONLY) == -1)
		return;

	if (cdb_find(&db->db, PKGDB_COUNT, strlen(PKGDB_COUNT)) <= 0) {
		warnx("corrupted database");
		return;
	}

	cdb_read(&db->db, &count, sizeof(count), cdb_datapos(&db->db));
	db->pkgs = calloc(count+1, sizeof(struct pkg *));

	/* Regex initialisation */
	if (match == MATCH_REGEX) {
		if (regcomp(&re, pattern, REG_BASIC | REG_NOSUB) != 0) {
			warnx("'%s' is not a valid regular expression", pattern);
			return;
		}
	} else if (match == MATCH_EREGEX) {
		if (regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
			warnx("'%s' is not a valid extended regular expression", pattern);
			return;
		}
	}

	for (i = 0; i < count; i++) {
		/* get package */
		if ((pkg = pkg_idx_query(&db->db, i)) == NULL)
			continue;

		if (pkg_match(pkg, &re, pattern, match) == 0) {
			if (db->flags & PKGDB_INIT_DEPS)
				pkg_get_deps(&db->db, pkg);
			if (db->flags & PKGDB_INIT_RDEPS)
				pkg_get_rdeps(&db->db, pkg, count);
			db->pkgs[db->count++] = pkg;
		}
		else
			free(pkg);
	}

	if (match == MATCH_REGEX || match == MATCH_EREGEX)
		regfree(&re);

	/* sort packages */
	db->pkgs = realloc(db->pkgs, (db->count+1) * sizeof(struct pkg *));
	db->pkgs[db->count] = NULL;
	qsort(db->pkgs, db->count, sizeof(struct pkg *), pkg_cmp);

	return;
}
