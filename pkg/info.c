#include <err.h>
#include <unistd.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>

#include "info.h"

/*
 *
 * list of options
 * -g: glob search: TODO
 * -x: regex search: TODO
 * -s: show package size: TODO
 * -S <type> : show scripts, type can be pre-install etc: TODO
 * -d: dependency list: TODO
 * -D: show reverse dependency list: TODO
 * -l: list contents of a package
 * -w <filename>: (which) finds which package the filename belongs to:
 * -e: return 1 of the package exist otherwise 0
 */

int
cmd_info(int argc, char **argv)
{
	struct pkgdb db;
	struct pkg *pkg;
	match_t match = MATCH_EXACT;
	int ch;

	while ((ch = getopt(argc, argv, "gxX")) != -1) {
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
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		match = MATCH_ALL;

	pkgdb_init(&db, argv[0], match);
	if (pkgdb_count(&db) == 1) {
		/* one match */
		pkg = db.pkgs[0];
		printf("Information for %s\n", pkg->name_version);
		printf("Comment:\n%s\n\n", pkg->comment);
		printf("Description:\n%s\n\n", pkg->desc);
	}
	else if (pkgdb_count(&db) > 1) {
		PKGDB_FOREACH(pkg, &db) {
			printf("%s: %s\n", pkg->name_version, pkg->comment);
		}
	}

	pkgdb_free(&db);
	return (0);
}
