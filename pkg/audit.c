#include <sys/param.h>
#include <sys/queue.h>

#define _WITH_GETLINE

#include <archive.h>
#include <archive_entry.h>
#include <err.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>

#include "audit.h"

#define AUDIT_URL "http://portaudit.FreeBSD.org/auditfile.tbz"

#define EQ 1
#define LT 2
#define LTE 3
#define GT 4
#define GTE 5
struct version_entry {
	char *version;
	int type;
};

struct audit_entry {
	char *pkgname;
	struct version_entry v1;
	struct version_entry v2;
	char *url;
	char *desc;
	SLIST_ENTRY(audit_entry) next;
};

SLIST_HEAD(audit_head, audit_entry);

void
usage_audit(void)
{
	fprintf(stderr, "usage: pkg audit [-F]\n");
}

static int
fetch_and_extract(const char *src, const char *dest)
{
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;
	int fd = -1;
	const char *tmp = "/tmp/auditfile.tbz";
	int retcode = EPKG_FATAL;
	int ret;

	if (pkg_fetch_file(src, tmp) != EPKG_OK) {
		warnx("Can't fetch audit file");
		goto cleanup;
	}

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	if (archive_read_open_filename(a, tmp, 4096) != ARCHIVE_OK) {
		warnx("archive_read_open_filename(%s): %s",
				tmp, archive_error_string(a));
		goto cleanup;
	}

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
		fd = open(dest, O_RDWR|O_CREAT|O_TRUNC,
				S_IRUSR|S_IRGRP|S_IROTH);
		if (fd < 0) {
			warn("open(%s)", dest);
			goto cleanup;
		}

		if (archive_read_data_into_fd(a, fd) != ARCHIVE_OK) {
			warnx("archive_read_data_into_fd(%s): %s",
					dest, archive_error_string(a));
			goto cleanup;
		}
	}

	retcode = EPKG_OK;

	cleanup:
	unlink(tmp);
	if (a != NULL)
		archive_read_finish(a);
	if (fd > 0)
		close(fd);

	return (retcode);
}

/* Fuuuu */
static void
parse_pattern(struct audit_entry *e, char *pattern, size_t len)
{
	size_t i;
	char *start = pattern;
	char *end;
	char **dest = &e->pkgname;
	char **next_dest = NULL;
	struct version_entry *v = &e->v1;
	int skipnext;
	int type;
	for (i = 0; i < len; i++) {
		type = 0;
		skipnext = 0;
		if (pattern[i] == '=') {
			type = EQ;
		}
		if (pattern[i] == '<') {
			if (pattern[i+1] == '=') {
				skipnext = 1;
				type = LTE;
			} else {
				type = LT;
			}
		}
		if (pattern[i] == '>') {
			if (pattern[i+1] == '=') {
				skipnext = 1;
				type = GTE;
			} else {
				type = GT;
			}
		}

		if (type != 0) {
			v->type = type;
			next_dest = &v->version;
			v = &e->v2;
		}

		if (next_dest != NULL || i == len - 1) {
			end = pattern + i;
			*dest = strndup(start, end - start);

			i += skipnext;
			start = pattern + i + 1;
			dest = next_dest;
			next_dest = NULL;
		}
	}
}

static int
parse_db(const char *path, struct audit_head *h)
{
	struct audit_entry *e;
	FILE *fp;
	size_t linecap = 0;
	ssize_t linelen;
	char *line = NULL;
	char *column;
	uint8_t column_id;

	if ((fp = fopen(path, "r")) == NULL)
	{
		warn("fopen(%s)", path);
		return EPKG_FATAL;
	}

	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		column_id = 0;

		if (line[0] == '#')
			continue;

		if ((e = calloc(1, sizeof(struct audit_entry))) == NULL)
			err(1, "calloc(audit_entry)");

		while ((column = strsep(&line, "|")) != NULL)
		{
			switch (column_id) {
				case 0:
					parse_pattern(e, column, linelen);
					break;
				case 1:
					e->url = strdup(column);
					break;
				case 2:
					e->desc = strdup(column);
					break;
				default:
					warn("extra column in audit file");
			}
			column_id++;
		}
		SLIST_INSERT_HEAD(h, e, next);
	}

	return EPKG_OK;
}

static bool
match_version(const char *pkgversion, struct version_entry *v)
{
	bool res = false;

	/*
	 * Return true so it is easier for the caller to handle case where there is
	 * only one version to match: the missing one will always match.
	 */ 
	if (v->version == NULL)
		return true;

	switch (pkg_version_cmp(pkgversion, v->version)) {
		case -1:
			if (v->type == LT || v->type == LTE)
				res = true;
			break;
		case 0:
			if (v->type == EQ || v->type == LTE || v->type == GTE)
				res = true;
			break;
		case 1:
			if (v->type == GT || v->type == GTE)
				res = true;
			break;
	}
	return res;
}

static bool
is_vulnerable(struct audit_head *h, struct pkg *pkg)
{
	struct audit_entry *e;
	const char *pkgname;
	const char *pkgversion;
	bool res = false, res1, res2;

	pkg_get(pkg,
		PKG_NAME, &pkgname,
		PKG_VERSION, &pkgversion
	);

	SLIST_FOREACH(e, h, next) {
		if (fnmatch(e->pkgname, pkgname, 0) != 0)
			continue;

		res1 = match_version(pkgversion, &e->v1);
		res2 = match_version(pkgversion, &e->v2);
		if (res1 && res2) {
			res = true;
			printf("%s-%s is vulnerable\n", pkgname, pkgversion);
			printf("%s\n", e->desc);
			printf("WWW: %s\n\n", e->url);
		}
	}

	return res;
}

static void
free_audit_list(struct audit_head *h)
{
	struct audit_entry *e;

	while (!SLIST_EMPTY(h)) {
		e = SLIST_FIRST(h);
		SLIST_REMOVE_HEAD(h, next);
		free(e->v1.version);
		free(e->v2.version);
		free(e->url);
		free(e->desc);
	}
}

int
exec_audit(int argc, char **argv)
{
	struct audit_head h = SLIST_HEAD_INITIALIZER();
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	const char *db_dir;
	char audit_file[MAXPATHLEN + 1];
	unsigned int vuln = 0;
	bool fetch = false;
	int ch;
	int ret;

	if (pkg_config_string(PKG_CONFIG_DBDIR, &db_dir) != EPKG_OK) {
		warnx("PKG_DBIR is missing");
		return (1);
	}
	snprintf(audit_file, sizeof(audit_file), "%s/auditfile", db_dir);

	while ((ch = getopt(argc, argv, "F")) != -1) {
		switch (ch) {
			case 'F':
				fetch = true;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (fetch == true) {
	       	if (fetch_and_extract(AUDIT_URL, audit_file) != EPKG_OK) {
			return (1);
		}
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (1);

	if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL)
	{
		warnx("Can not query local database");
		goto cleanup;
	}

	if (parse_db(audit_file, &h) != EPKG_OK) {
		warnx("Can not parse and load audit db");
		goto cleanup;
	}

	while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		if (is_vulnerable(&h, pkg)) {
			vuln++;
		}
	}

	printf("%u problem(s) in your installed packages found.\n", vuln);

cleanup:
	if (db != NULL)
		pkgdb_close(db);
	if (it != NULL)
		pkgdb_it_free(it);
	if (pkg != NULL)
		pkg_free(pkg);
	free_audit_list(&h);

	return (0);
}
