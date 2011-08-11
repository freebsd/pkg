#include <sys/types.h>

#include <err.h>
#include <inttypes.h>
#include <libutil.h>
#include <pkg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "info.h"

enum sign {
	LT,
	LE,
	GT,
	GE,
	EQ
};

static int
print_info(struct pkg * const pkg, unsigned int opt)
{
	struct pkg_dep *dep = NULL;
	struct pkg_file *file = NULL;
	struct pkg_category *cat = NULL;
	struct pkg_license *lic = NULL;
	struct pkg_option *option = NULL;
	char size[7];

	if (opt & INFO_FULL) {
		printf("Name: %s\n", pkg_get(pkg, PKG_NAME));
		printf("Version: %s\n", pkg_get(pkg, PKG_VERSION));
		printf("Origin: %s\n", pkg_get(pkg, PKG_ORIGIN));
		printf("Prefix: %s\n", pkg_get(pkg, PKG_PREFIX));
		printf("Categories:");
		while (pkg_categories(pkg, &cat) == EPKG_OK)
			printf(" %s", pkg_category_name(cat));
		printf("\n");
		printf("Licenses: ");
		while (pkg_licenses(pkg, &lic) == EPKG_OK) {
			printf(" %s", pkg_license_name(lic));
			if (pkg_licenselogic(pkg) != 1)
				printf(" %c", pkg_licenselogic(pkg));
			else
				printf(" ");
		}
		printf("\b \n");
		printf("Maintainer: %s\n", pkg_get(pkg, PKG_MAINTAINER));
		printf("WWW: %s\n", pkg_get(pkg, PKG_WWW));
		printf("Comment: %s\n", pkg_get(pkg, PKG_COMMENT));
		printf("Options: \n");
		while (pkg_options(pkg, &option) == EPKG_OK)
			printf("\t%s: %s\n", pkg_option_opt(option), pkg_option_value(option));
		humanize_number(size, sizeof(size), pkg_flatsize(pkg), "B", HN_AUTOSCALE, 0);
		printf("Flat size: %s\n", size);
		printf("Description:\n%s\n", pkg_get(pkg, PKG_DESC));
		printf("\n");
	} else if (opt & INFO_PRINT_DEP) {
		if (!(opt & INFO_QUIET))
			printf("%s-%s depends on:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			printf("%s-%s\n", pkg_dep_name(dep), pkg_dep_version(dep));
		}

		if (!(opt & INFO_QUIET))
			printf("\n");
	} else if (opt & INFO_PRINT_RDEP) {
		if (!(opt & INFO_QUIET))
			printf("%s-%s is required by:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

		while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
			printf("%s-%s\n", pkg_dep_name(dep), pkg_dep_version(dep));
		}

		if (!(opt & INFO_QUIET))
			printf("\n");
	} else if (opt & INFO_LIST_FILES) {
		if (!(opt & INFO_QUIET))
			printf("%s-%s owns the following files:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

		while (pkg_files(pkg, &file) == EPKG_OK) {
			printf("%s\n", pkg_file_path(file));
		}

		if (!(opt & INFO_QUIET))
			printf("\n");
	} else if (opt & INFO_SIZE) {
		humanize_number(size, sizeof(size), pkg_flatsize(pkg), "B", HN_AUTOSCALE, 0);
		printf("%s-%s size is %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), size);
	} else if (opt & INFO_ORIGIN) {
		if (opt & INFO_QUIET)
			printf("%s\n", pkg_get(pkg, PKG_ORIGIN));
		else
			printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_ORIGIN));
	} else if (opt & INFO_PREFIX) {
		if (opt & INFO_QUIET)
			printf("%s\n", pkg_get(pkg, PKG_PREFIX));
		else
			printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_PREFIX));
	} else {
		if (opt & INFO_QUIET)
			printf("%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		else
			printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_COMMENT));
	}

	return (0);
}

void
usage_info(void)
{
	fprintf(stderr, "usage: pkg info -a\n");
	fprintf(stderr, "       pkg info [-egxXdrlsqOf] <pkg-name>\n");
	fprintf(stderr, "       pkg info [-drlsqf] -F <pkg-file>\n\n");
	fprintf(stderr, "For more information see 'pkg help info'.\n");
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
	int query_flags = PKG_LOAD_BASIC;
	struct pkg *pkg = NULL;
	unsigned int opt = 0;
	match_t match = MATCH_EXACT;
	char *pkgname;
	char *pkgversion;
	const char *file = NULL;
	int ch;
	int ret = EPKG_OK;
	int retcode = 0;
	bool gotone = false;
	int i;
	int sign = 0;

	/* TODO: exclusive opts ? */
	while ((ch = getopt(argc, argv, "aegxXEdrlsqopOfF:")) != -1) {
		switch (ch) {
			case 'a':
				match = MATCH_ALL;
				break;
			case 'O':
				opt |= INFO_ORIGIN_SEARCH;  /* this is only for ports compat */
				break;
			case 'e':
				opt |= INFO_EXISTS;
				retcode = 1;
				break;
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
				query_flags |= PKG_LOAD_DEPS;
				break;
			case 'r':
				opt |= INFO_PRINT_RDEP;
				query_flags |= PKG_LOAD_RDEPS;
				break;
			case 'l':
				opt |= INFO_LIST_FILES;
				query_flags |= PKG_LOAD_FILES;
				break;
			case 's':
				opt |= INFO_SIZE;
				break;
			case 'E': /* ports compatibility */
			case 'q':
				opt |= INFO_QUIET;
				break;
			case 'o':
				opt |= INFO_ORIGIN;
				break;
			case 'p':
				opt |= INFO_PREFIX;
				break;
			case 'f':
				opt |= INFO_FULL;
				query_flags |= PKG_LOAD_CATEGORIES|PKG_LOAD_LICENSES|PKG_LOAD_OPTIONS;
				break;
			case 'F':
				file = optarg;
				break;
			default:
				usage_info();
				return(EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0 && file == NULL && match != MATCH_ALL) {
		/* which -O bsd.*.mk always execpt clean output */
		if (opt & INFO_ORIGIN_SEARCH)
			return (EX_OK);
		usage_info();
		return (EX_USAGE);
	}

	if (file != NULL) {
		if (pkg_open(&pkg, file) != EPKG_OK) {
			return (1);
		}
		print_info(pkg, opt);
		pkg_free(pkg);
		return (0);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	i = 0;
	do {
		pkgname = argv[i];

		/*
		 * allow to search for origin with a trailing /
		 * likes audio/linux-vsound depending on ${PORTSDIR}/audio/sox/
		 */

		if (argc > 0 && pkgname[strlen(pkgname) -1] == '/')
			pkgname[strlen(pkgname) -1] = '\0';

		if (argc > 0) {
			pkgversion = strrchr(pkgname, '>');
			if (pkgversion == NULL)
				pkgversion = strrchr(pkgname, '<');
			if (pkgversion == NULL)
				pkgversion = strrchr(pkgname, '=');
		} else
			pkgversion = NULL;

		if (pkgversion != NULL) {
			switch (pkgversion[0]) {
				case '>':
					pkgversion[0] = '\0';
					pkgversion++;
					sign = GT;
					if (pkgversion[0] == '=') {
						pkgversion++;
						sign=GE;
					}
					break;
				case '<':
					pkgversion[0] = '\0';
					pkgversion++;
					sign = LT;
					if (pkgversion[0] == '=') {
						pkgversion++;
						sign=LE;
					}
					break;
				case '=':
					pkgversion[0] = '\0';
					pkgversion++;
					sign = EQ;
					break;
			}
		}

		if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
			return (EX_IOERR);
		}

		/* this is place for compatibility hacks */

		/* ports infrastructure expects pkg info -q -O to always return 0 even
		 * if the ports doesn't exists */
		if (opt & INFO_ORIGIN_SEARCH)
			gotone = true;

		/* end of compatibility hacks */

		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
			gotone = true;
			if (pkgversion != NULL && pkgversion[0] != '0') { /* FIXME special workaround for >=0 */
				switch (pkg_version_cmp(pkg_get(pkg, PKG_VERSION), pkgversion)) {
					case -1:
						if (sign != LT && sign != LE) {
							gotone = false;
							continue;
						}
						break;
					case 0:
						if (sign != LE && sign != GE && sign != EQ) {
							gotone = false;
							continue;
						}
						break;
					case 1:
						if (sign != GT && sign != GE) {
							gotone = false;
							continue;
						}
						break;
				}
			}
			if (opt & INFO_EXISTS)
				retcode = 0;
			else
				print_info(pkg, opt);
		}
		if (ret != EPKG_END) {
			retcode = 1;
		}

		if (retcode == 0 && !gotone && match != MATCH_ALL)
			retcode = EX_SOFTWARE;

		pkgdb_it_free(it);

		i++;
	} while (i < argc);

	pkg_free(pkg);
	pkgdb_close(db);

	return (retcode);
}
