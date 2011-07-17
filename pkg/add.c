#include <sys/param.h>
#include <sys/types.h>

#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "add.h"

static int
is_url(const char *pattern)
{
	if (strncmp(pattern, "http://", 7) == 0 ||
		strncmp(pattern, "https://", 8) == 0 ||
		strncmp(pattern, "ftp://", 6) == 0)
		return (EPKG_OK);

	return (EPKG_FATAL);
}

void
usage_add(void)
{
	fprintf(stderr, "usage: pkg add <pkg-name>\n");
	fprintf(stderr, "       pkg add <url>://<pkg-name>\n\n");
	fprintf(stderr, "For more information see 'pkg help add'.\n");
}

int
exec_add(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	char path[MAXPATHLEN];
	char *file;
	int retcode = EPKG_OK;
	int i;

	if (argc < 2) {
		usage_add();
		return (-1);
	}

	if (geteuid() != 0) {
		warnx("adding packages can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	for (i = 1; i < argc; i++) {
		if (is_url(argv[i]) == EPKG_OK) {
			snprintf(path, sizeof(path), "./%s", basename(argv[i]));
			if ((retcode = pkg_fetch_file(argv[i], path)) != EPKG_OK) {
				continue;
			}
			file = path;
		} else
			file = argv[i];

		if (pkg_add(db, file) != EPKG_OK) {
			continue;
		}
	}

	cleanup:
	pkgdb_close(db);

	return (retcode == EPKG_OK ? 0 : 1);
}

