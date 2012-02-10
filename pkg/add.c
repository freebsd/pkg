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

#include "pkgcli.h"

static int
is_url(const char * const pattern)
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
	struct sbuf *failedpkgs = sbuf_new_auto();
	char path[MAXPATHLEN + 1];
	char *file;
	int retcode = EPKG_OK;
	int i;
	int failedpkgcount = 0;
	struct pkg *p = NULL;

	if (argc < 2) {
		usage_add();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("adding packages can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	for (i = 1; i < argc; i++) {
		if (is_url(argv[i]) == EPKG_OK) {
			snprintf(path, sizeof(path), "./%s", basename(argv[i]));
			if ((retcode = pkg_fetch_file(argv[i], path)) != EPKG_OK)
				break;

			file = path;
		} else
			file = argv[i];
			
		pkg_open(&p, file, NULL);

		if ((retcode = pkg_add(db, file, 0)) != EPKG_OK) {
			sbuf_cat(failedpkgs, argv[i]);
			if (i != argc - 1)
				sbuf_printf(failedpkgs, ", ");
			failedpkgcount++;
		}

	}

	pkgdb_close(db);
	
	if(failedpkgcount > 0) {
		sbuf_finish(failedpkgs);
		printf("\nFailed to install the following %d package(s): %s.\n", failedpkgcount, sbuf_data(failedpkgs));
	}
	sbuf_delete(failedpkgs);

	if (messages != NULL) {
		sbuf_finish(messages);
		printf("%s", sbuf_data(messages));
	}

	return (retcode == EPKG_OK ? EX_OK : EX_SOFTWARE);
}

