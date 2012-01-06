#include <sys/types.h>

#include <err.h>
#include <inttypes.h>
#include <pkg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "info.h"
#include "utils.h"

enum sign {
	LT,
	LE,
	GT,
	GE,
	EQ
};

void
usage_info(void)
{
	fprintf(stderr, "usage: pkg info <pkg-name>\n");
	fprintf(stderr, "       pkg info -a\n");
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
		if (pkg_open(&pkg, file, NULL) != EPKG_OK) {
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
					/* compatibility pkg_info accept == and = the same way */
					if (pkgname[0] != '=' && pkgversion[-1] == '=')
						pkgversion[-1] = '\0';
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
			const char *version;

			pkg_get(pkg, PKG_VERSION, &version);
			if (pkgversion != NULL) {
				switch (pkg_version_cmp(version, pkgversion)) {
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
