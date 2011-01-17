#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "register.h"

void
usage_register(void)
{
	fprintf(stderr, "register ...\n"
			"register\n");
}

int
exec_register(int argc, char **argv)
{
	struct pkg *pkg;
	struct pkgdb *db;

	char ch;
	char *plist = NULL;
	char *prefix = NULL;
	char *depends = NULL;
	char *conflicts = NULL;
	char *v = NULL;

	int ret = 0;

	pkg_new(&pkg);
	while ((ch = getopt(argc, argv, "vc:d:f:p:P:m:o:C:n:M:s:")) != -1) {
		switch (ch) {
			case 'v':
				/* IGNORE */
				break;
			case 'c':
				ret += pkg_set(pkg, PKG_COMMENT, optarg[0] == '-' ? optarg + 1 : optarg);
				break;
			case 'd':
				ret += pkg_set_from_file(pkg, PKG_DESC, optarg);
				break;
			case 'f':
				plist = strdup(optarg);
				break;
			case 'p':
				prefix = strdup(optarg);
				break;
			case 'P':
				depends = strdup(optarg);
				break;
			case 'm':
				ret += pkg_set_from_file(pkg, PKG_MTREE, optarg);
				break;
			case 'n':
				v = strrchr(optarg, '-');
				v[0] = '\0';
				v++;
				ret += pkg_set(pkg, PKG_NAME, optarg);
				ret += pkg_set(pkg, PKG_VERSION, v);
				break;
			case 'o':
				ret += pkg_set(pkg, PKG_ORIGIN, optarg);
				break;
			case 'C':
				conflicts = strdup(optarg);
				break;
			case 'M':
				ret += pkg_set_from_file(pkg, PKG_MESSAGE, optarg);
				break;
			default:
				printf("%c\n", ch);
				usage_register();
				return (-1);
		}
	}

	if (ret < 0) {
		pkg_free(pkg);
		return (ret);
	}

	if (depends != NULL) {
		ret += ports_parse_depends(pkg, depends);
		if (ret < 0)
			return (ret);
	}

	if (conflicts != NULL) {
		ret += ports_parse_conflicts(pkg, conflicts);
		if (ret < 0)
			return (ret);
	}

	ret += ports_parse_plist(pkg, plist, prefix);

	if (ret < 0)
		return (ret);

	if (prefix != NULL)
		free(prefix);

	if (plist != NULL)
		free(plist);

	if (conflicts != NULL)
		free(conflicts);

	if (depends != NULL)
		free(depends);

	if (pkgdb_open(&db) == -1) {
		pkgdb_warn(db);
		return (-1);
	}

	pkgdb_register_pkg(db, pkg);
	pkgdb_close(db);
	pkg_free(pkg);

	return (0);
}
