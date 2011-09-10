#include <sys/types.h>
#include <sys/sbuf.h>

#include <inttypes.h>
#include <libutil.h>
#include <pkg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "query.h"

static void
format_str(struct pkg *pkg, struct sbuf *dest, const char *qstr, void *data)
{
	char size[7];

	sbuf_clear(dest);

	while (qstr[0] != '\0') {
		if (qstr[0] == '%') {
			qstr++;
			switch (qstr[0]) {
				case 'n':
					sbuf_cat(dest, pkg_get(pkg, PKG_NAME));
					break;
				case 'v':
					sbuf_cat(dest, pkg_get(pkg, PKG_VERSION));
					break;
				case 'o':
					sbuf_cat(dest, pkg_get(pkg, PKG_ORIGIN));
					break;
				case 'p':
					sbuf_cat(dest, pkg_get(pkg, PKG_PREFIX));
					break;
				case 'm':
					sbuf_cat(dest, pkg_get(pkg, PKG_MAINTAINER));
					break;
				case 'c':
					sbuf_cat(dest, pkg_get(pkg, PKG_COMMENT));
					break;
				case 'w':
					sbuf_cat(dest, pkg_get(pkg, PKG_WWW));
					break;
				case 'a':
					sbuf_printf(dest, "%d", pkg_isautomatic(pkg));
					break;
				case 's':
					qstr++;
					if (qstr[0] == 'h') {
						humanize_number(size, sizeof(size), pkg_flatsize(pkg), "B", HN_AUTOSCALE, 0);
						sbuf_cat(dest, size);
					} else if (qstr[0] == 'b') {
						sbuf_printf(dest, "%" PRId64, pkg_flatsize(pkg));
					}
					break;
				case '?':
					qstr++;
					switch (qstr[0]) {
						case 'd':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_DEPS));
							break;
						case 'r':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_RDEPS));
							break;
						case 'C':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_CATEGORIES));
							break;
						case 'F':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_FILES));
							break;
						case 'O':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_OPTIONS));
							break;
						case 'D':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_DIRS));
							break;
						case 'L':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_LICENSES));
							break;
						case 'U':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_USERS));
							break;
						case 'G':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_GROUPS));
							break;
						case 'K':
							sbuf_printf(dest, "%d", !pkg_list_isempty(pkg, PKG_CONFLICTS));
							break;
					}
					break;
				case 'l':
					if (pkg_licenselogic(pkg))
						sbuf_putc(dest, pkg_licenselogic(pkg));
					break;
				case 'd':
					qstr++;
					if (qstr[0] == 'n')
						sbuf_cat(dest, pkg_dep_name((struct pkg_dep *)data));
					else if (qstr[0] == 'o')
						sbuf_cat(dest, pkg_dep_origin((struct pkg_dep *)data));
					else if (qstr[0] == 'v')
						sbuf_cat(dest, pkg_dep_version((struct pkg_dep *)data));
					break;
				case 'r':
					qstr++;
					if (qstr[0] == 'n')
						sbuf_cat(dest, pkg_dep_name((struct pkg_dep *)data));
					else if (qstr[0] == 'o')
						sbuf_cat(dest, pkg_dep_origin((struct pkg_dep *)data));
					else if (qstr[0] == 'v')
						sbuf_cat(dest, pkg_dep_version((struct pkg_dep *)data));
					break;
				case 'C':
					sbuf_cat(dest, pkg_category_name((struct pkg_category *)data));
					break;
				case 'F':
					qstr++;
					if (qstr[0] == 'p')
						sbuf_cat(dest, pkg_file_path((struct pkg_file *)data));
					else if (qstr[0] == 's')
						sbuf_cat(dest, pkg_file_sha256((struct pkg_file *)data));
					break;
				case 'S':
					/* TODO */
					break;
				case 'O':
					qstr++;
					if (qstr[0] == 'k')
						sbuf_cat(dest, pkg_option_opt((struct pkg_option *)data));
					else if (qstr[0] == 'v')
						sbuf_cat(dest, pkg_option_value((struct pkg_option *)data));
					break;
				case 'D':
					sbuf_cat(dest, pkg_dir_path((struct pkg_dir *)data));
					break;
				case 'L':
					sbuf_cat(dest, pkg_license_name((struct pkg_license *)data));
					break;
				case 'U':
					sbuf_cat(dest, pkg_user_name((struct pkg_user *)data));
					break;
				case 'G':
					sbuf_cat(dest, pkg_group_name((struct pkg_group *)data));
					break;
				case 'K':
					sbuf_cat(dest, pkg_conflict_glob((struct pkg_conflict *)data));
					break;
				case '%':
					sbuf_putc(dest, '%');
					break;
			}
		} else  if (qstr[0] == '\\') {
			qstr++;
			switch (qstr[0]) {
				case 'n':
					sbuf_putc(dest, '\n');
					break;
				case 'a':
					sbuf_putc(dest, '\a');
					break;
				case 'b':
					sbuf_putc(dest, '\b');
					break;
				case 'f':
					sbuf_putc(dest, '\f');
					break;
				case 'r':
					sbuf_putc(dest, '\r');
					break;
				case '\\':
					sbuf_putc(dest, '\\');
					break;
				case 't':
					sbuf_putc(dest, '\t');
					break;
			}
		} else {
			sbuf_putc(dest, qstr[0]);
		}
		qstr++;
	}
	sbuf_finish(dest);
}
static void
print_query(struct pkg *pkg, char *qstr, char multiline)
{
	struct sbuf *output = sbuf_new_auto();
	struct pkg_dep *dep = NULL;
	struct pkg_category *cat = NULL;
	struct pkg_option *option = NULL;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	struct pkg_license *lic = NULL;
	struct pkg_user *user = NULL;
	struct pkg_group *group = NULL;
	struct pkg_conflict *conflict = NULL;

	switch (multiline) {
		case 'd':
			while (pkg_deps(pkg, &dep) == EPKG_OK) {
				format_str(pkg, output, qstr, dep);
				printf("%s\n", sbuf_data(output));
				break;
		}
		case 'r':
			while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
				format_str(pkg, output, qstr, dep);
				printf("%s\n", sbuf_data(output));
			}
			break;
		case 'C':
			while (pkg_categories(pkg, &cat) == EPKG_OK) {
				format_str(pkg, output, qstr, cat);
				printf("%s\n", sbuf_data(output));
			}
			break;
		case 'O':
			while (pkg_options(pkg, &option) == EPKG_OK) {
				format_str(pkg, output, qstr, option);
				printf("%s\n", sbuf_data(output));
			}
			break;
		case 'F':
			while (pkg_files(pkg, &file) == EPKG_OK) {
				format_str(pkg, output, qstr, file);
				printf("%s\n", sbuf_data(output));
			}
			break;
		case 'D':
			while (pkg_dirs(pkg, &dir) == EPKG_OK) {
				format_str(pkg, output, qstr, dir);
				printf("%s\n", sbuf_data(output));
			}
			break;
		case 'L':
			while (pkg_licenses(pkg, &lic) == EPKG_OK) {
				format_str(pkg, output, qstr, lic);
				printf("%s\n", sbuf_data(output));
			}
			break;
		case 'U':
			while (pkg_users(pkg, &user) == EPKG_OK) {
				format_str(pkg, output, qstr, user);
				printf("%s\n", sbuf_data(output));
			}
			break;
		case 'G':
			while (pkg_users(pkg, &user) == EPKG_OK) {
				format_str(pkg, output, qstr, group);
				printf("%s\n", sbuf_data(output));
			}
			break;
		case 'K':
			while (pkg_conflicts(pkg, &conflict) == EPKG_OK) {
				format_str(pkg, output, qstr, conflict);
				printf("%s\n", sbuf_data(output));
			}
			break;
		default:
			format_str(pkg, output, qstr, dep);
			printf("%s\n", sbuf_data(output));
			break;
	}
	sbuf_delete(output);
}
static int
analyse_query_string(char *qstr, int *flags, char *multiline)
{
	while (qstr[0] != '\0') {
		if (qstr[0] == '%') {
			qstr++;
			switch (qstr[0]) {
				case 'd':
					qstr++;
					if (qstr[0] != 'n' && qstr[0] != 'o' && qstr[0] != 'v') {
						fprintf(stderr, "Invalid query: %%d should be followed by: n, o or v\n");
						return (EPKG_FATAL);
					}
					if (*multiline != 0 && *multiline != 'd') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%d' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'd';
					*flags |= PKG_LOAD_DEPS;
					break;
				case 'r':
					qstr++;
					if (qstr[0] != 'n' && qstr[0] != 'o' && qstr[0] != 'v') {
						fprintf(stderr, "Invalid query: %%r should be followed by: n, o or v\n");
						return (EPKG_FATAL);
					}
					if (*multiline != 0 && *multiline != 'r') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%r' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'r';
					*flags |= PKG_LOAD_RDEPS;
					break;
				case 'C':
					if (*multiline != 0 && *multiline != 'C') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%c' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'C';
					*flags |= PKG_LOAD_CATEGORIES;
					break;
				case 'F':
					qstr++;
					if (qstr[0] != 'p' && qstr[0] != 's') {
						fprintf(stderr, "Invalid query: %%F should be followed by: p or s\n");
						return (EPKG_FATAL);
					}
					if (*multiline != 0 && *multiline != 'F') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%F' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'F';
					*flags |= PKG_LOAD_FILES;
					break;
				case 'S':
					if (*multiline != 0 && *multiline != 'S') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%S' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'S';
					*flags |= PKG_LOAD_SCRIPTS;
					break;
				case 'O':
					qstr++;
					if (qstr[0] != 'k' && qstr[0] != 'v') {
						fprintf(stderr, "Invalid query: %%O should be followed by: k or v\n");
						return (EPKG_FATAL);
					}
					if (*multiline != 0 && *multiline != 'O') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%O' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'O';
					*flags |= PKG_LOAD_OPTIONS;
					break;
				case 'D':
					if (*multiline != 0 && *multiline != 'D') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%D' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'D';
					*flags |= PKG_LOAD_DIRS;
					break;
				case 'L':
					if (*multiline != 0 && *multiline != 'L') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%L' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'L';
					*flags |= PKG_LOAD_LICENSES;
					break;
				case 'U':
					if (*multiline != 0 && *multiline != 'U') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%U' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'U';
					*flags |= PKG_LOAD_USERS;
					break;
				case 'G':
					if (*multiline != 0 && *multiline != 'G') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%G' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'G';
					*flags |= PKG_LOAD_GROUPS;
					break;
				case 'K':
					if (*multiline != 0 && *multiline != 'K') {
						fprintf(stderr, "Invalid query format string, you can't query '%%%c' and '%%K' at the same time\n", *multiline);
						return (EPKG_FATAL);
					}
					*multiline = 'K';
					*flags |= PKG_LOAD_CONFLICTS;
					break;
				case 's':
					qstr++;
					if (qstr[0] != 'h' && qstr[0] != 'b') {
						fprintf(stderr, "Invalid query: %%s should be followed by: h or b\n");
						return (EPKG_FATAL);
					}
					break;
				case '?':
					qstr++;
					if (qstr[0] != 'd' && qstr[0] != 'r' && qstr[0] != 'C' &&
							qstr[0] != 'F' && qstr[0] != 'O' &&
							qstr[0] != 'D' && qstr[0] != 'L' &&
							qstr[0] != 'U' && qstr[0] != 'G' &&
							qstr[0] != 'K' ) {
						fprintf(stderr, "Invalid query: %%? should be followed by: d, r, C, F, O, D, L, U, G or K\n");
						return (EPKG_FATAL);
					}
					switch (qstr[0]) {
						case 'd':
							*flags |= PKG_LOAD_DEPS;
							break;
						case 'r':
							*flags |= PKG_LOAD_RDEPS;
							break;
						case 'C':
							*flags |= PKG_LOAD_CATEGORIES;
							break;
						case 'F':
							*flags |= PKG_LOAD_FILES;
							break;
						case 'O':
							*flags |= PKG_LOAD_OPTIONS;
							break;
						case 'D':
							*flags |= PKG_LOAD_DIRS;
							break;
						case 'L':
							*flags |= PKG_LOAD_LICENSES;
							break;
						case 'U':
							*flags |= PKG_LOAD_USERS;
							break;
						case 'G':
							*flags |= PKG_LOAD_GROUPS;
							break;
						case 'K':
							*flags |= PKG_LOAD_CONFLICTS;
							break;
					}

					break;
				case 'n':
				case 'v':
				case 'o':
				case 'p':
				case 'm':
				case 'c':
				case 'w':
				case 'l':
				case 'a':
					/* this is ok */
					break;
				default:
					fprintf(stderr, "Unkown query format key: '%%%c'\n", qstr[0]);
					return (EPKG_FATAL);
			}
		}
		qstr++;
	}

	return (EPKG_OK);
}
void
usage_query(void)
{
	fprintf(stderr, "usage: pkg query -a <query-format>\n");
	fprintf(stderr, "       pkg query [-gxX] <query-format> <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help query.\n");
}

int
exec_query(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	char *pkgname = NULL;
	int query_flags = PKG_LOAD_BASIC;
	match_t match = MATCH_EXACT;
	int ch;
	int ret = EPKG_OK;
	int retcode = EXIT_SUCCESS;
	int i;
	char multiline = 0;

	while ((ch = getopt(argc, argv, "agxX")) != -1) {
		switch (ch) {
			case 'a':
				match = MATCH_ALL;
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
			default:
				usage_query();
				return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		usage_query();
		return (EX_USAGE);
	}

	if ((argc == 1) ^ (match == MATCH_ALL)) {
		usage_query();
		return (EX_USAGE);
	}

	if (analyse_query_string(argv[0], &query_flags, &multiline) != EPKG_OK)
		return (EX_USAGE);

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (match == MATCH_ALL) {
		if ((it = pkgdb_query(db, NULL, match)) == NULL)
			return (EX_IOERR);

		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK)
			print_query(pkg, argv[0],  multiline);

		if (ret != EPKG_END)
			retcode = EX_SOFTWARE;

		pkgdb_it_free(it);
	} else {
		for (i = 1; i < argc; i++) {
			pkgname = argv[i];

			/* remove trailing / is any for origin */
			if (argc > 0 && pkgname[strlen(pkgname) -1] == '/')
				pkgname[strlen(pkgname) -1] = '\0';

			if ((it = pkgdb_query(db, pkgname, match)) == NULL)
				return (EX_IOERR);

			while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK)
				print_query(pkg, argv[0], multiline);

			if (ret != EPKG_END) {
				retcode = EX_SOFTWARE;
				break;
			}

			pkgdb_it_free(it);
		}
	}

	pkg_free(pkg);
	pkgdb_close(db);

	return (retcode);
}
