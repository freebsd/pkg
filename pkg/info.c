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
	struct pkg *pkg, **deps;
	unsigned char flags = 0;
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
				flags |= PKGDB_INIT_DEPS;
				opt |= INFO_PRINT_DEP;
				break;
			case 'D':
				flags |= PKGDB_INIT_RDEPS;
				opt |= INFO_PRINT_RDEP;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		match = MATCH_ALL;

	pkgdb_init(&db, argv[0], match, flags);

	PKGDB_FOREACH(pkg, &db) {

		if (opt & INFO_PRINT_DEP) {
			printf("%s depends on:\n", pkg->name_version);
			for (deps = pkg->deps; *deps != NULL; deps++)
				printf("%s\n", (*deps)->name_version);
		}

		else if (opt & INFO_PRINT_RDEP) {
			printf("%s is required for:\n", pkg->name_version);
			for (deps = pkg->rdeps; *deps != NULL; deps++)
				printf("%s\n", (*deps)->name_version);
		}

		else if (pkgdb_count(&db) == 1) {
			printf("Informations for %s\n\n", pkg->name_version);
			printf("Comment:\n%s\n\n", pkg->comment);
			printf("Description:\n%s\n\n", pkg->desc);
		}

		else {
			printf("%s: %s\n", pkg->name_version, pkg->comment);
		}
	}

	pkgdb_free(&db);
	return (0);
}
