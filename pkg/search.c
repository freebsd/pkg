#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sysexits.h>

#include <pkg.h>

#include "search.h"
#include "utils.h"

void
usage_search(void)
{
	fprintf(stderr, "usage: pkg search <pkg-name>\n");
	fprintf(stderr, "       pkg search [-fDsqop] <pkg-name>\n");
	fprintf(stderr, "       pkg search [-gxXcdfDsqop] <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help search'.\n");
}

int
exec_search(int argc, char **argv)
{
	int retcode = EPKG_OK, ch;
	int flags = PKG_LOAD_BASIC;
	unsigned int opt = 0;
	match_t match = MATCH_EXACT;
	pkgdb_field field = FIELD_NAME;
	const char *pattern = NULL;
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;

	while ((ch = getopt(argc, argv, "gxXcdfDsqop")) != -1) {
		switch (ch) {
			case 'g':
				match = MATCH_GLOB;
				break;
			case 'x':
				match = MATCH_REGEX;
				break;
			case 'X':
				match = MATCH_EREGEX;
				break;
			case 'c':
				field = FIELD_COMMENT;
				break;
			case 'd':
				field = FIELD_DESC;
				break;
			case 'f':
				opt |= INFO_FULL;
				flags |= PKG_LOAD_CATEGORIES|PKG_LOAD_LICENSES|PKG_LOAD_OPTIONS;
				break;
			case 'D':
				opt |= INFO_PRINT_DEP;
				flags |= PKG_LOAD_DEPS;
				break;
			case 's':
				opt |= INFO_SIZE;
				break;
			case 'q':
				opt |= INFO_QUIET;
				break;
			case 'o':
				opt |= INFO_ORIGIN;
				break;
			case 'p':
				opt |= INFO_PREFIX;
				break;
			default:
				usage_search();
				return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage_search();
		return (EX_USAGE);
	}

	pattern = argv[0];

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		return (EX_IOERR);

	if ((it = pkgdb_rquery(db, pattern, match, field)) == NULL) {
		pkgdb_close(db);
		return (1);
	}

	while ((retcode = pkgdb_it_next(it, &pkg, flags)) == EPKG_OK)
		print_info(pkg, opt);

	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (retcode);
}
