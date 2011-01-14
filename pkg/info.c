#include <sys/types.h>
#include <sys/stat.h>

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

static int64_t
pkg_size(struct pkg *pkg)
{
	struct pkg_file **files;
	struct stat st;
	int64_t size = 0;

	files = pkg_files(pkg);
	for (size_t i = 0; files[i] != NULL; i++) {
		if (stat(pkg_file_path(files[i]), &st) != 0) {
			warn("stat(%s)", pkg_file_path(files[i]));
			continue;
		}
		size += st.st_size;
	}
	return (size);
}

static int
query_pkg(struct pkg *pkg, unsigned char opt) {
	struct pkg **deps;
	struct pkg_file **files;
	int i;

	if (opt & INFO_PRINT_DEP) {
		printf("%s-%s depends on: \n", pkg_name(pkg), pkg_version(pkg));

		deps = pkg_deps(pkg);
		for (i = 0; deps[i] != NULL; i++) {
			printf("%s-%s\n", pkg_name(deps[i]), pkg_version(deps[i]));
		}

		printf("\n");
	} else if (opt & INFO_LIST_FILES) {
		printf("%s-%s owns the following files:\n", pkg_name(pkg), pkg_version(pkg));

		files = pkg_files(pkg);
		for (i = 0; files[i] != NULL; i++) {
			printf("%s\n", pkg_file_path(files[i]));
		}

		printf("\n");
	} else {
		printf("%s-%s: %s\n", pkg_name(pkg), pkg_version(pkg), pkg_comment(pkg));
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
	struct pkg *pkg;
	struct pkg **deps;
	struct pkg_file **files;
	unsigned char opt = 0;
	char size[7];
	match_t match = MATCH_EXACT;
	char ch;
	size_t i;
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
				query_flags = PKG_FILES;
				break;
			case 's':
				opt |= INFO_SIZE;
				query_flags = PKG_FILES;
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

	/* if the last argument is a file then query directly the file */
	if (argc == 1 && pkg_open(argv[0], &pkg, query_flags) == 0)
		return (query_pkg(pkg, opt));

	if (pkgdb_open(&db) == -1) {
		pkgdb_warn(db);
		return (-1);
	}

	if ((it = pkgdb_query(db, argv[0], match)) == NULL) {
		pkgdb_warn(db);
		return (-1);
	}

	/* this is place for compatibility hacks */

	/* ports infrastructure expects pkg info -q -O to always return 0 even
	 * if the ports doesn't exists */
	if (opt & INFO_ORIGIN_SEARCH)
		gotone = true;


	/* end of compatibility hacks */
	pkg_new(&pkg);

	while (pkgdb_it_next_pkg(it, &pkg, query_flags) == 0) {
		gotone = true;

		if (opt & INFO_EXISTS) {
			retcode = 0;
		} else if (opt & INFO_PRINT_DEP) {
			printf("%s-%s depends on:\n", pkg_name(pkg), pkg_version(pkg));

			deps = pkg_deps(pkg);
			for (i = 0; deps[i] != NULL; i++) {
				printf("%s-%s\n", pkg_name(deps[i]), pkg_version(deps[i]));
			}

			printf("\n");
		} else if (opt & INFO_PRINT_RDEP) {
			printf("%s-%s is required by:\n", pkg_name(pkg), pkg_version(pkg));

			deps = pkg_rdeps(pkg);
			for (i = 0; deps[i] != NULL; i++) {
				printf("%s-%s\n", pkg_name(deps[i]), pkg_version(deps[i]));
			}

			printf("\n");
		} else if (opt & INFO_LIST_FILES) {
			printf("%s-%s owns the following files:\n", pkg_name(pkg), pkg_version(pkg));

			files = pkg_files(pkg);
			for (i = 0; files[i] != NULL; i++) {
				printf("%s\n", pkg_file_path(files[i]));
			}

			printf("\n");
		} else if (opt & INFO_SIZE) {
			humanize_number(size, sizeof(size), pkg_size(pkg), "B", HN_AUTOSCALE, 0);
			printf("%s-%s size is %s\n", pkg_name(pkg), pkg_version(pkg), size);
		} else if (opt & INFO_ORIGIN) {
			if (opt & INFO_QUIET)
				printf("%s\n", pkg_origin(pkg));
			else
				printf("%s-%s: %s\n", pkg_name(pkg), pkg_version(pkg), pkg_origin(pkg));

		} else {
			if (opt & INFO_QUIET)
				printf("%s-%s\n", pkg_name(pkg), pkg_version(pkg));
			else
				printf("%s-%s: %s\n", pkg_name(pkg), pkg_version(pkg), pkg_comment(pkg));
		}
	}
	pkg_free(pkg);

	if (pkgdb_errnum(db) > 0) {
		pkgdb_warn(db);
		retcode = -1;
	}

	if (retcode == 0 && !gotone && match != MATCH_ALL)
		retcode = EX_SOFTWARE;

	pkgdb_it_free(it);
	pkgdb_close(db);

	return (retcode);
}
