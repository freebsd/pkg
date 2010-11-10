#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <inttypes.h>
#include <libutil.h>
#include <unistd.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>

#include "info.h"


static int64_t
pkg_size(struct pkg *pkg)
{
	struct stat st;
	const char *path;
	const char *md5;
	int64_t size = 0;

	while(pkg_files(pkg, &path, &md5) == 0) {
		if (stat(path, &st) != 0) {
			warn("stat(%s)", path);
			continue;
		}
		size += st.st_size;
	}
	return (size);
}

/*
 * list of options
 * -S <type> : show scripts, type can be pre-install etc: TODO
 */

int
cmd_info(int argc, char **argv)
{
	struct pkgdb *db;
	struct pkg *pkg;
	struct pkg *dep;
	const char *path;
	const char *md5;
	unsigned char opt = 0;
	char size[7];
	match_t match = MATCH_EXACT;
	char ch;
	int retcode = 0;

	/* TODO: exclusive opts ? */
	while ((ch = getopt(argc, argv, "egxXdrls")) != -1) {
		switch (ch) {
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
				break;
			case 'r':
				opt |= INFO_PRINT_RDEP;
				break;
			case 'l':
				opt |= INFO_LIST_FILES;
				break;
			case 's':
				opt |= INFO_SIZE;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		match = MATCH_ALL;

	if (pkgdb_open(&db) == -1) {
		pkgdb_warn(db);
		return (-1);
	}

	if (pkgdb_query_init(db, argv[0], match) == -1) {
		pkgdb_warn(db);
		return (-1);
	}

	pkg_new(&pkg);
	pkg_new(&dep);

	while (pkgdb_query(db, pkg) == 0) {
		if (opt & INFO_EXISTS) {
			retcode = 0;
		} else if (opt & INFO_PRINT_DEP) {

			printf("%s-%s depends on:\n", pkg_name(pkg), pkg_version(pkg));

			while (pkg_dep(pkg, dep) == 0) {
				printf("%s-%s\n", pkg_name(dep), pkg_version(pkg));
			}

			printf("\n");
		} else if (opt & INFO_PRINT_RDEP) {
			printf("%s-%s is required by:\n", pkg_name(pkg), pkg_version(pkg));
			while (pkg_rdep(pkg, dep) == 0) {
				printf("%s-%s\n", pkg_name(dep), pkg_version(dep));
			}
			printf("\n");
		} else if (opt & INFO_LIST_FILES) {
			printf("%s-%s owns the following files:\n", pkg_name(pkg), pkg_version(pkg));
			while (pkg_files(pkg, &path, &md5) == 0) {
				printf("%s\n", path);
			}
		} else if (opt & INFO_SIZE) {
			humanize_number(size, sizeof(size), pkg_size(pkg), "B", HN_AUTOSCALE, 0);
			printf("%s-%s size is %s\n", pkg_name(pkg), pkg_version(pkg), size);
		} else {
			printf("%s-%s: %s\n", pkg_name(pkg), pkg_version(pkg), pkg_comment(pkg));
		}
	}

	pkg_free(pkg);
	pkg_free(dep);

	if (pkgdb_errnum(db) > 0) {
		pkgdb_warn(db);
		retcode = -1;
	}

	pkgdb_query_free(db);
	pkgdb_close(db);
	return (retcode);
}
