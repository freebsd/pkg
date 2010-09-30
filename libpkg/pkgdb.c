#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <err.h>
#include <fnmatch.h>
#include <regex.h>

#include "pkgdb.h"
#include "pkgdb_cache.h"

const char *
pkgdb_get_dir(void)
{
	const char *pkg_dbdir;

	if ((pkg_dbdir = getenv("PKG_DBDIR")) == NULL)
		pkg_dbdir = PKG_DBDIR;

	return pkg_dbdir;
}

static int
pkgdb_open(struct pkgdb *db)
{
	char path[MAXPATHLEN];
	int fd;

	snprintf(path, sizeof(path), "%s/pkgdb.cache", pkgdb_get_dir());

	if ((fd = open(path, O_RDONLY)) == -1)
		warn("open(%s):", path);
	else
		fd = cdb_init(&db->db, fd);

	return (fd);
}

/* query formated string -> string ('\0'-terminated) */
static const char *
pkgdb_query(struct pkgdb *db, const char *fmt, ...)
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

	if (cdb_find(&db->db, key, len) < 0)
		return NULL;

	db_get(string, &db->db);
	return (string);
}

/* query a pkg from db using index */
static struct pkg *
pkgdb_pkg_query(struct pkgdb *db, size_t idx)
{
	struct pkg *pkg;

	if (db == NULL)
		return NULL;

	if (cdb_find(&db->db, &idx, sizeof(idx)) <= 0)
		return NULL;

	pkg = malloc(sizeof(*pkg));
	pkg->idx = idx;
	db_get(pkg->name, &db->db);

	pkg->name_version = pkgdb_query(db, PKGDB_NAMEVER, idx);
	pkg->version = pkgdb_query(db, PKGDB_VERSION, idx);
	pkg->comment = pkgdb_query(db, PKGDB_COMMENT, idx);
	pkg->desc    = pkgdb_query(db, PKGDB_DESC, idx);
	pkg->origin  = pkgdb_query(db, PKGDB_ORIGIN, idx);

	return (pkg);
}

/* populate deps on pkg */
static void
pkgdb_deps_query(struct pkgdb *db, struct pkg *pkg)
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

	cdb_findinit(&cdbf, &db->db, key, klen);

	while (cdb_findnext(&cdbf) > 0)
		count++;

	pkg->deps = calloc(count+1, sizeof(*pkg->deps));
	pkg->deps[count] = NULL;

	cdb_findinit(&cdbf, &db->db, key, klen);

	j = 0;
	while (cdb_findnext(&cdbf) > 0) {
		db_get(name_version, &db->db);
		name = strdup(name_version);

		if ((version = strrchr(name, '-')) == NULL) {
			free(name);
			continue;
		}

		*(version++) = '\0';

		/* get index */
		if (cdb_find(&db->db, name, strlen(name)) <= 0) {
			free(name);
			continue;
		}
		free(name);

		cdb_read(&db->db, &idx, sizeof(idx), cdb_datapos(&db->db));

		/* get package */
		if ((dep = pkgdb_pkg_query(db, idx)) == NULL) {
			/* partial package */
			dep = calloc(1, sizeof(*dep));
			dep->name_version = name_version;
			dep->errors |= PKGERR_NOT_INSTALLED;
		} else { /* package exist */
			if (strcmp(version, dep->version) != 0)
				dep->errors |= PKGERR_VERSION_MISMATCH;
		}
		pkg->deps[j++] = dep;
	}
}

/* populate rdeps on package */
static void
pkgdb_rdeps_query(struct pkgdb *db, struct pkg *pkg, size_t count)
{
	size_t i, j;
	struct pkg *p, **deps;

	if (db == NULL || pkg == NULL)
		return;

	pkg->rdeps = calloc(count+1, sizeof(struct pkg *));

	for (i = 0, j = 0; i < count; i++) {

		if ((p = pkgdb_pkg_query(db, i)) == NULL)
			continue;

		pkgdb_deps_query(db, p);

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
pkgdb_init(struct pkgdb *db, const char *pattern, match_t match, unsigned char flags) {
	/* first check if the cache has to be rebuild */
	pkgdb_cache_update();
	size_t count, i;
	struct pkg *pkg;
	regex_t re;

	db->count = 0;
	db->flags = flags;

	if (match != MATCH_ALL && pattern == NULL) {
		warnx("a pattern is required");
		return;
	}

	if (pkgdb_open(db) == -1)
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
		if ((pkg = pkgdb_pkg_query(db, i)) == NULL)
			continue;

		if (pkg_match(pkg, &re, pattern, match) == 0) {
			if (db->flags & PKGDB_INIT_DEPS)
				pkgdb_deps_query(db, pkg);
			if (db->flags & PKGDB_INIT_RDEPS)
				pkgdb_rdeps_query(db, pkg, count);
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

static void
pkg_free(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg **deps;
	if (db->flags & PKGDB_INIT_DEPS) {
		if (!(pkg->errors & PKGERR_NOT_INSTALLED)) {
			for (deps = pkg->deps; *deps != NULL; deps++) {
				pkg_free(db, *deps);
			}
		}
	}
	free(pkg);
}

void
pkgdb_free(struct pkgdb *db)
{
	int fd;
	struct pkg *pkg, **deps;

	fd = cdb_fileno(&db->db);
	cdb_free(&db->db);
	close(fd);

	PKGDB_FOREACH(pkg, db) {
		if (db->flags & PKGDB_INIT_DEPS) {
			for (deps = pkg->deps; *deps != NULL; deps++)
				free(*deps);
			free(pkg->deps);
		}
		if (db->flags & PKGDB_INIT_RDEPS) {
			for (deps = pkg->rdeps; *deps != NULL; deps++)
				free(*deps);
			free(pkg->rdeps);
		}
		free(pkg);
	}

	free(db->pkgs);
}

size_t
pkgdb_count(struct pkgdb *db)
{
	return (db->count);
}


