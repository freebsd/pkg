#include <sys/types.h>
#include <sys/sbuf.h>

#include <inttypes.h>
#include <libutil.h>
#include <pkg.h>
#include <pkg_util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "query.h"

static struct query_flags {
        const char flag;
        const char *options;
        const unsigned multiline;
        const int dbflags;
} q_flags[] = {
        { 'd', "nov",		1, PKG_LOAD_DEPS },
        { 'r', "nov",		1, PKG_LOAD_RDEPS },
        { 'C', "",		1, PKG_LOAD_CATEGORIES },
        { 'F', "ps",		1, PKG_LOAD_FILES }, 
        { 'S', "",		1, PKG_LOAD_SCRIPTS },
        { 'O', "kv",		1, PKG_LOAD_OPTIONS },
        { 'D', "",		1, PKG_LOAD_DIRS },
        { 'L', "",		1, PKG_LOAD_LICENSES },
        { 'U', "",		1, PKG_LOAD_USERS },
        { 'G', "",		1, PKG_LOAD_GROUPS },
	{ '?', "drCFODLUGK",	1, PKG_LOAD_BASIC },	/* dbflags handled in analyse_query_string() */
        { 's', "hb",		0, PKG_LOAD_BASIC }, 
        { 'n', "",		0, PKG_LOAD_BASIC },
        { 'v', "",		0, PKG_LOAD_BASIC },
        { 'o', "",		0, PKG_LOAD_BASIC },
        { 'p', "",		0, PKG_LOAD_BASIC },
        { 'm', "",		0, PKG_LOAD_BASIC },
        { 'c', "",		0, PKG_LOAD_BASIC },
        { 'w', "",		0, PKG_LOAD_BASIC },
        { 'l', "",		0, PKG_LOAD_BASIC },
        { 'a', "",		0, PKG_LOAD_BASIC },
};

const unsigned int flags_len = (sizeof(q_flags)/sizeof(q_flags[0]));

static void
format_str(struct pkg *pkg, struct sbuf *dest, const char *qstr, void *data)
{
	char size[7];
	const char *tmp;
	bool automatic;
	int64_t flatsize;
	lic_t licenselogic;

	sbuf_clear(dest);

	while (qstr[0] != '\0') {
		if (qstr[0] == '%') {
			qstr++;
			switch (qstr[0]) {
				case 'n':
					pkg_get(pkg, PKG_NAME, &tmp);
					sbuf_cat(dest, tmp);
					break;
				case 'v':
					pkg_get(pkg, PKG_VERSION, &tmp);
					sbuf_cat(dest, tmp);
					break;
				case 'o':
					pkg_get(pkg, PKG_ORIGIN, &tmp);
					sbuf_cat(dest, tmp);
					break;
				case 'p':
					pkg_get(pkg, PKG_PREFIX, &tmp);
					sbuf_cat(dest, tmp);
					break;
				case 'm':
					pkg_get(pkg, PKG_MAINTAINER, &tmp);
					sbuf_cat(dest, tmp);
					break;
				case 'c':
					pkg_get(pkg, PKG_COMMENT, &tmp);
					sbuf_cat(dest, tmp);
					break;
				case 'w':
					pkg_get(pkg, PKG_WWW, &tmp);
					sbuf_cat(dest, tmp);
					break;
				case 'a':
					pkg_get(pkg, PKG_AUTOMATIC, &automatic);
					sbuf_printf(dest, "%d", automatic);
					break;
				case 's':
					qstr++;
					pkg_get(pkg, PKG_FLATSIZE, &flatsize);
					if (qstr[0] == 'h') {
						humanize_number(size, sizeof(size), flatsize, "B", HN_AUTOSCALE, 0);
						sbuf_cat(dest, size);
					} else if (qstr[0] == 'b') {
						sbuf_printf(dest, "%" PRId64, flatsize);
					}
					break;
				case '?':
					qstr++;
					switch (qstr[0]) {
						case 'd':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_DEPS));
							break;
						case 'r':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_RDEPS));
							break;
						case 'C':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_CATEGORIES));
							break;
						case 'F':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_FILES));
							break;
						case 'O':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_OPTIONS));
							break;
						case 'D':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_DIRS));
							break;
						case 'L':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_LICENSES));
							break;
						case 'U':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_USERS));
							break;
						case 'G':
							sbuf_printf(dest, "%d", !pkg_list_is_empty(pkg, PKG_GROUPS));
							break;
					}
					break;
				case 'l':
					pkg_get(pkg, PKG_LICENSE_LOGIC, &licenselogic);
					switch (licenselogic) {
						case LICENSE_SINGLE:
							sbuf_cat(dest, "single");
							break;
						case LICENSE_OR:
							sbuf_cat(dest, "or");
							break;
						case LICENSE_AND:
							sbuf_cat(dest, "and");
							break;
					}
					break;
				case 'd':
					qstr++;
					if (qstr[0] == 'n')
						sbuf_cat(dest, pkg_dep_get((struct pkg_dep *)data, PKG_DEP_NAME));
					else if (qstr[0] == 'o')
						sbuf_cat(dest, pkg_dep_get((struct pkg_dep *)data, PKG_DEP_ORIGIN));
					else if (qstr[0] == 'v')
						sbuf_cat(dest, pkg_dep_get((struct pkg_dep *)data, PKG_DEP_VERSION));
					break;
				case 'r':
					qstr++;
					if (qstr[0] == 'n')
						sbuf_cat(dest, pkg_dep_get((struct pkg_dep *)data, PKG_DEP_NAME));
					else if (qstr[0] == 'o')
						sbuf_cat(dest, pkg_dep_get((struct pkg_dep *)data, PKG_DEP_ORIGIN));
					else if (qstr[0] == 'v')
						sbuf_cat(dest, pkg_dep_get((struct pkg_dep *)data, PKG_DEP_VERSION));
					break;
				case 'C':
					sbuf_cat(dest, pkg_category_name((struct pkg_category *)data));
					break;
				case 'F':
					qstr++;
					if (qstr[0] == 'p')
						sbuf_cat(dest, pkg_file_get((struct pkg_file *)data, PKG_FILE_PATH));
					else if (qstr[0] == 's')
						sbuf_cat(dest, pkg_file_get((struct pkg_file *)data, PKG_FILE_SUM));
					break;
				case 'S':
					sbuf_cat(dest, pkg_script_data((struct pkg_script *)data));	
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
	struct pkg_script *scripts = NULL;

	switch (multiline) {
		case 'd':
			while (pkg_deps(pkg, &dep) == EPKG_OK) {
				format_str(pkg, output, qstr, dep);
				printf("%s\n", sbuf_get(output));
				break;
		}
		case 'r':
			while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
				format_str(pkg, output, qstr, dep);
				printf("%s\n", sbuf_get(output));
			}
			break;
		case 'C':
			while (pkg_categories(pkg, &cat) == EPKG_OK) {
				format_str(pkg, output, qstr, cat);
				printf("%s\n", sbuf_get(output));
			}
			break;
		case 'O':
			while (pkg_options(pkg, &option) == EPKG_OK) {
				format_str(pkg, output, qstr, option);
				printf("%s\n", sbuf_get(output));
			}
			break;
		case 'F':
			while (pkg_files(pkg, &file) == EPKG_OK) {
				format_str(pkg, output, qstr, file);
				printf("%s\n", sbuf_get(output));
			}
			break;
		case 'D':
			while (pkg_dirs(pkg, &dir) == EPKG_OK) {
				format_str(pkg, output, qstr, dir);
				printf("%s\n", sbuf_get(output));
			}
			break;
		case 'L':
			while (pkg_licenses(pkg, &lic) == EPKG_OK) {
				format_str(pkg, output, qstr, lic);
				printf("%s\n", sbuf_get(output));
			}
			break;
		case 'U':
			while (pkg_users(pkg, &user) == EPKG_OK) {
				format_str(pkg, output, qstr, user);
				printf("%s\n", sbuf_get(output));
			}
			break;
		case 'G':
			while (pkg_users(pkg, &user) == EPKG_OK) {
				format_str(pkg, output, qstr, group);
				printf("%s\n", sbuf_get(output));
			}
			break;
		case 'S':
			while (pkg_scripts(pkg, &scripts) == EPKG_OK) {
				format_str(pkg, output, qstr, scripts);
				printf("%s\n", sbuf_get(output));
			}
			break;
		default:
			format_str(pkg, output, qstr, dep);
			printf("%s\n", sbuf_get(output));
			break;
	}
	sbuf_delete(output);
}
static int
analyse_query_string(char *qstr, int *flags, char *multiline)
{
	unsigned int i, j, k;
	unsigned int valid_flag = 0;
	unsigned int valid_opts = 0;

	while (qstr[0] != '\0') {
		if (qstr[0] == '%') {
			qstr++;
			valid_flag = 0;
	
			for (i = 0; i < flags_len; i++) {
				/* found the flag */
				if (qstr[0] == q_flags[i].flag) {
					valid_flag = 1;
					
					/* if the flag is followed by additional options */
					if (q_flags[i].options[0] != '\0') {
						qstr++;
						valid_opts = 0;

						for (j = 0; j < strlen(q_flags[i].options); j++) {
							if (qstr[0] == q_flags[i].options[j]) {
								valid_opts = 1;
								break;
							}
						}
						
						if (valid_opts == 0) {
							fprintf(stderr, "Invalid query: '%%%c' should be followed by:", q_flags[i].flag);

							for (j = 0; j < strlen(q_flags[i].options); j++)
								fprintf(stderr, " %c%c", q_flags[i].options[j],
										q_flags[i].options[j + 1] == '\0' ?
										'\n' : ',');

							return (EPKG_FATAL);
						}
					}

					/* if this is a multiline flag */
					if (q_flags[i].multiline == 1) {
						if (*multiline != 0 && *multiline != q_flags[i].flag) {
							fprintf(stderr, "Invalid query: you cannot query '%%%c' and '%%%c' at the same time\n",
									*multiline, q_flags[i].flag);
							return (EPKG_FATAL);
						} else {
							*multiline = q_flags[i].flag;
						}
					}

					/* handle the '?' flag cases */
					if (q_flags[i].flag == '?') {
						for (k = 0; k < flags_len; k++) 
							if (q_flags[k].flag == q_flags[i].options[j]) {
								*flags |= q_flags[k].dbflags;
								break;
							}
					} else {
						*flags |= q_flags[i].dbflags;
					}

					break; /* don't iterate over the rest of the flags */
				}
			}

			if (valid_flag == 0) {
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
	fprintf(stderr, "       pkg query -f <pkg-name> <query-format>\n");
	fprintf(stderr, "       pkg query [-gxX] <query-format> <pattern> <...>\n\n");
	fprintf(stderr, "For more information see 'pkg help query.'\n");
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

	while ((ch = getopt(argc, argv, "agxXf:")) != -1) {
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
			case 'f':
				pkgname = optarg;
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

	if ((argc == 1) ^ (match == MATCH_ALL) && pkgname == NULL) {
		usage_query();
		return (EX_USAGE);
	}

	if (analyse_query_string(argv[0], &query_flags, &multiline) != EPKG_OK)
		return (EX_USAGE);

	if (pkgname != NULL) {
		if (pkg_open(&pkg, pkgname, NULL) != EPKG_OK) {
			return (1);
		}
		
		print_query(pkg, argv[0], multiline);
		pkg_free(pkg);
		return (0);
	}

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
