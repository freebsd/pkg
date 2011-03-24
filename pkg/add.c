#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <pkg.h>

#include "add.h"

static void
fetch_status(void *data, const char *url, off_t total, off_t done, time_t elapsed)
{
	int percent;

	data = NULL;
	elapsed = 0;

	percent = ((float)done / (float)total) * 100;
	printf("\rFetching %s... %d%%", url, percent);

	if (done == total)
		printf("\n");

	fflush(stdout);
}

static int
is_url(const char *pattern)
{
	if (strnstr(pattern, "http://", 7) == 0 ||
		strnstr(pattern, "https://", 8) == 0 ||
		strnstr(pattern, "ftp://", 6) == 0)
		return (0);

	return (-1);
}

void
usage_add(void)
{
	fprintf(stderr, "add ... <pkg-name>\n"
			"add\n");
}

int
exec_add(int argc, char **argv)
{
	struct pkgdb *db;
	char *file;

	if (argc != 2) {
		usage_add();
		return (-1);
	}

	if (pkgdb_open(&db) != EPKG_OK) {
		pkg_error_warn("can not open database");
		pkgdb_close(db);
		return (-1);
	}

	if (is_url(argv[1]) == 0) {
		asprintf(&file, "./%s", basename(argv[1]));
		if (pkg_fetch_file(argv[1], file, NULL, &fetch_status) != EPKG_OK) {
			pkg_error_warn("can not fetch %s", argv[1]);
			return (1);
		}
	} else
		file = argv[1];

	if (pkg_add(db, file) != EPKG_OK)
		pkg_error_warn("can not install %s", file);

	pkgdb_close(db);

	return (0);
}
