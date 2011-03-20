#include <sys/types.h>

#include <err.h>
#include <inttypes.h>
#include <libutil.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <pkg.h>
#include <string.h>
#include <sysexits.h>

#include "info.h"

static int
query_pkg(struct pkg *pkg, unsigned char opt) {
	struct pkg **deps;
	struct pkg_file **files;
	int i;

	if (opt & INFO_PRINT_DEP) {
		printf("%s-%s depends on: \n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

		deps = pkg_deps(pkg);
		for (i = 0; deps[i] != NULL; i++) {
			printf("%s-%s\n", pkg_get(deps[i], PKG_NAME), pkg_get(deps[i], PKG_VERSION));
		}

		printf("\n");
	} else if (opt & INFO_LIST_FILES) {
		printf("%s-%s owns the following files:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

		files = pkg_files(pkg);
		for (i = 0; files[i] != NULL; i++) {
			printf("%s\n", pkg_file_path(files[i]));
		}

		printf("\n");
	} else {
		printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_COMMENT));
	}

	pkg_free(pkg);

	return (0);
}

void
usage_info(void)
{
	fprintf(stderr, "info [-egxXdrlsqO] <pkg-name>\n"
			"info\n");
}

/*
 * list of options
 * -S <type> : show scripts, type can be pre-install etc: TODO
 */

int
exec_info(int argc, char **argv)
{
	struct pkgdb *db;
	struct pkgdb_it *it;
	int query_flags = PKG_BASIC;
	struct pkg *pkg = NULL;
	struct pkg **deps;
	struct pkg_file **files;
	unsigned char opt = 0;
	char size[7];
	match_t match = MATCH_EXACT;
	char ch;
	size_t i;
	int ret;
	int retcode = 0;
	bool gotone = false;

	/* TODO: exclusive opts ? */
	while ((ch = getopt(argc, argv, "egxXdrlsqoO")) != -1) {
		switch (ch) {
			case 'O':
				opt |= INFO_ORIGIN_SEARCH;  /* this is only for ports compat */
				break;
			case 'e':
				opt |= INFO_EXISTS;
				retcode = 1;
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
				query_flags |= PKG_DEPS;
				break;
			case 'r':
				opt |= INFO_PRINT_RDEP;
				query_flags |= PKG_RDEPS;
				break;
			case 'l':
				opt |= INFO_LIST_FILES;
				query_flags |= PKG_FILES;
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
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		match = MATCH_ALL;

	pkg_new(&pkg);

	/* if the last argument is a file then query directly the file */
	if (argc == 1 && access(argv[0], F_OK) == 0) {
		if (pkg_open(&pkg, argv[0]) != EPKG_OK) {
			warnx("can not read package %s", argv[0]);
			return (-1);
		}
		return (query_pkg(pkg, opt));
	}

	if (pkgdb_open(&db) != EPKG_OK) {
		pkg_error_warn("can not open database");
		pkgdb_close(db);
		return (-1);
	}

	if ((it = pkgdb_query(db, argv[0], match)) == NULL) {
		pkg_error_warn("can not query database");
		return (-1);
	}

	/* this is place for compatibility hacks */

	/* ports infrastructure expects pkg info -q -O to always return 0 even
	 * if the ports doesn't exists */
	if (opt & INFO_ORIGIN_SEARCH)
		gotone = true;

	/* end of compatibility hacks */

	while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
		gotone = true;

		if (opt & INFO_EXISTS) {
			retcode = 0;
		} else if (opt & INFO_PRINT_DEP) {
			if (!(opt & INFO_QUIET))
				printf("%s-%s depends on:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

			deps = pkg_deps(pkg);
			for (i = 0; deps[i] != NULL; i++) {
				printf("%s-%s\n", pkg_get(deps[i], PKG_NAME), pkg_get(deps[i], PKG_VERSION));
			}

			if (!(opt & INFO_QUIET))
				printf("\n");
		} else if (opt & INFO_PRINT_RDEP) {
			printf("%s-%s is required by:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

			deps = pkg_rdeps(pkg);
			for (i = 0; deps[i] != NULL; i++) {
				printf("%s-%s\n", pkg_get(deps[i], PKG_NAME), pkg_get(deps[i], PKG_VERSION));
			}

			printf("\n");
		} else if (opt & INFO_LIST_FILES) {
			printf("%s-%s owns the following files:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

			files = pkg_files(pkg);
			for (i = 0; files[i] != NULL; i++) {
				printf("%s\n", pkg_file_path(files[i]));
			}

			printf("\n");
		} else if (opt & INFO_SIZE) {
			humanize_number(size, sizeof(size), pkg_flatsize(pkg), "B", HN_AUTOSCALE, 0);
			printf("%s-%s size is %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), size);
		} else if (opt & INFO_ORIGIN) {
			if (opt & INFO_QUIET)
				printf("%s\n", pkg_get(pkg, PKG_ORIGIN));
			else
				printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_ORIGIN));

		} else {
			if (opt & INFO_QUIET)
				printf("%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
			else
				printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_COMMENT));
		}
	}
	pkg_free(pkg);

	if (ret != EPKG_END) {
		pkg_error_warn("can not iterate over results");
		retcode = -1;
	}

	if (retcode == 0 && !gotone && match != MATCH_ALL)
		retcode = EX_SOFTWARE;

	pkgdb_it_free(it);
	pkgdb_close(db);

	return (retcode);
}
