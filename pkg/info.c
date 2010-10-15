#include <err.h>
#include <unistd.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>
#include <string.h>

#include "info.h"

/*
 * list of options
 * -s: show package size: TODO
 * -S <type> : show scripts, type can be pre-install etc: TODO
 * -D: show reverse dependency list: TODO
 * -l: list contents of a package
 * -w <filename>: (which) finds which package the filename belongs to:
 * -e: return 1 if the package exist otherwise 0
 */

int
cmd_info(int argc, char **argv)
{
	struct pkgdb db;
	struct pkg pkg, dep;
	unsigned char opt = 0;
	match_t match = MATCH_EXACT;
	char ch;

	/* TODO: exclusive opts ? */
	while ((ch = getopt(argc, argv, "gxXdD")) != -1) {
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
			case 'd':
				opt |= INFO_PRINT_DEP;
				break;
			case 'D':
				opt |= INFO_PRINT_RDEP;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		match = MATCH_ALL;

	if (pkgdb_init(&db, argv[0], match) == -1) {
		pkgdb_warn(&db);
		return (-1);
	}

	while (pkgdb_query(&db, &pkg) == 0) {
		if (opt & INFO_PRINT_DEP) {

			printf("%s-%s depends on:\n", pkg_name(&pkg), pkg_version(&pkg));

			while (pkg_dep(&pkg, &dep) == 0) {
				printf("%s-%s\n", pkg_name(&dep), pkg_version(&pkg));
			}

			printf("\n");
		} else if (opt & INFO_PRINT_RDEP) {
			printf("%s-%s is required by:\n", pkg_name(&pkg), pkg_version(&pkg));
			while (pkg_rdep(&pkg, &dep) == 0) {
				printf("%s-%s\n", pkg_name(&dep), pkg_version(&dep));
			}
			printf("\n");
		} else {
			printf("%s-%s: %s\n", pkg_name(&pkg), pkg_version(&pkg), pkg_comment(&pkg));
		}
	}

	if (db.errnum > -1) {
		pkgdb_warn(&db);
		pkgdb_free(&db);
		return (-1);
	}

	pkgdb_free(&db);
	return (0);
}
