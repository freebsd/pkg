#include <sys/param.h>
#include <sys/queue.h>

#define _WITH_GETLINE

#include <archive.h>
#include <archive_entry.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>

#include "audit.h"

#define AUDIT_URL "http://portaudit.FreeBSD.org/auditfile.tbz"

struct audit_entry {
	char *pattern;
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
	if (ae != NULL)
		archive_entry_free(ae);
	if (fd > 0)
		close(fd);

	return (retcode);
}

static int
parse_db(const char *path, struct audit_head *h)
{
	struct audit_entry *e;
	FILE *fp;
	size_t linecap = 0;
	ssize_t linelen;
	char *line = NULL;
	const char *column;
	uint8_t column_id;

	if ((fp = fopen(path, "r")) == NULL)
	{
		warn("fopen(%s)", path);
		return 1;
	}

	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		column_id = 0;

		if ((e = calloc(1, sizeof(struct audit_entry))) == NULL)
			err(1, "calloc(audit_entry)");

		while ((column = strsep(&line, "|")) != NULL)
		{
			switch (column_id) {
				case 0:
					e->pattern = strdup(column);
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

	return 0;
}

static void
free_audit_list(struct audit_head *h)
{
	struct audit_entry *e;

	while (!SLIST_EMPTY(h)) {
		e = SLIST_FIRST(h);
		SLIST_REMOVE_HEAD(h, next);
		free(e->pattern);
		free(e->url);
		free(e->desc);
	}
}

int
exec_audit(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	const char *db_dir;
	char audit_file[MAXPATHLEN + 1];
	bool fetch = false;
	int ch;
	struct pkgdb_it *it = NULL;
	struct audit_head h = SLIST_HEAD_INITIALIZER();
#if 0
	struct pkg *pkg = NULL;
	struct pkg *p = NULL;
#endif

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

	if (fetch == true && fetch_and_extract(AUDIT_URL, audit_file) != 0)
		return (1);

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (1);

	if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL)
	{
		warnx("Can not query local database");
		goto cleanup;
	}

	if (parse_db(audit_file, &h) != 0) {
		warnx("Can not parse and load audit db");
		goto cleanup;
	}

	/* TODO: do it! seriously!! */


cleanup:
	if (db != NULL)
		pkgdb_close(db);
	if (it != NULL)
		pkgdb_it_free(it);
	free_audit_list(&h);

	return (0);
}
