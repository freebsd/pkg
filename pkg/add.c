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

static void
fetch_status(void *data, const char *url, off_t total, off_t done, time_t elapsed)
{
	unsigned int percent;

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
	if (strncmp(pattern, "http://", 7) == 0 ||
		strncmp(pattern, "https://", 8) == 0 ||
		strncmp(pattern, "ftp://", 6) == 0)
		return (0);

	return (-1);
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
	struct pkg *pkg = NULL;
	char *file;
	const char *message;
	int retcode = 0;

	if (argc != 2) {
		usage_add();
		return (-1);
	}

	if (geteuid() != 0) {
		warnx("adding packages can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT, R_OK|W_OK) != EPKG_OK) {
		pkg_error_warn("can not open database");
		return (1);
	}

	if (is_url(argv[1]) == 0) {
		asprintf(&file, "./%s", basename(argv[1]));
		if (pkg_fetch_file(argv[1], file, NULL, &fetch_status) != EPKG_OK) {
			pkg_error_warn("can not fetch %s", argv[1]);
			retcode = 1;
			goto cleanup;
		}
	} else
		file = argv[1];

	if (pkg_add(db, file, &pkg) != EPKG_OK) {
		pkg_error_warn("can not install %s", file);
		retcode = 1;
		goto cleanup;
	}

	message = pkg_get(pkg, PKG_MESSAGE);
	if (message != NULL && message[0] != '\0')
		printf("%s", message);

	cleanup:

	if (db != NULL)
		pkgdb_close(db);

	if (pkg != NULL)
		pkg_free(pkg);

	return (retcode);
}

