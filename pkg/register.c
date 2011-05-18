#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/utsname.h>
#include <regex.h>

#include "register.h"

static struct {
	const pkg_attr attr;
	const char * const flag;
} required_flags[] = {
	{ PKG_ORIGIN, "-o"},
	{ PKG_NAME, "-n"},
	{ PKG_VERSION, "-n"},
	{ PKG_COMMENT, "-c"},
	{ PKG_DESC, "-d"},
	{ PKG_PREFIX, "-p"},
	{ PKG_MAINTAINER, "-r"},
	{ 0, NULL}
};

void
usage_register(void)
{
	fprintf(stderr, "usage: pkg register -c comment -d desc -f plist_file -p prefix\n");
	fprintf(stderr, "                    -m mtree_file -n pkgname -o origin -r maintainer\n");
	fprintf(stderr, "                    [-P depends] [-C conflicts] [-M message_file] [-s scripts]\n");
	fprintf(stderr, "                    [-a arch] [-w www] [-O options] [-H] [-i input_dir]\n");
	fprintf(stderr, "                    [-l]\n\n");
	fprintf(stderr, "For more information see 'pkg help register'.\n");
}

int
exec_register(int argc, char **argv)
{
	struct pkg *pkg;
	struct pkgdb *db;
	struct utsname u;

	regex_t preg;
	regmatch_t pmatch[2];

	int ch;
	char *plist = NULL;
	char *v = NULL;
	char *arch = NULL;
	char *www = NULL;
	char *input_path = NULL;

	const char *desc = NULL;
	size_t size;

	bool heuristic = false;
	bool legacy = false;

	int retcode = 0;
	int ret = 0;

	if (geteuid() != 0) {
		warnx("registering packages can only be done as root");
		return (EX_NOPERM);
	}

	pkg_new(&pkg);
	while ((ch = getopt(argc, argv, "vHc:d:f:p:P:m:o:C:n:M:s:a:r:w:O:i:l")) != -1) {
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
				if ((plist = strdup(optarg)) == NULL) 
					errx(1, "cannot allocate memory");

				break;
			case 'p':
				pkg_set(pkg, PKG_PREFIX, optarg);
				break;
			case 'P':
				ret += ports_parse_depends(pkg, optarg);
				break;
			case 'm':
				ret += pkg_set_from_file(pkg, PKG_MTREE, optarg);
				break;
			case 'n':
				if ((v = strrchr(optarg, '-')) == NULL)
					errx(EX_DATAERR, "bad pkgname format");

				v[0] = '\0';
				v++;
				ret += pkg_set(pkg, PKG_NAME, optarg);
				ret += pkg_set(pkg, PKG_VERSION, v);
				break;
			case 'o':
				ret += pkg_set(pkg, PKG_ORIGIN, optarg);
				break;
			case 'C':
				ret += ports_parse_conflicts(pkg, optarg);
				break;
			case 'M':
				ret += pkg_set_from_file(pkg, PKG_MESSAGE, optarg);
				break;
			case 's':
				ret += ports_parse_scripts(pkg, optarg);
				break;
			case 'a':
				if ((arch = strdup(optarg)) == NULL)
					errx(1, "cannot allocate memory");

				break;
			case 'r': /* responsible */
				ret += pkg_set(pkg, PKG_MAINTAINER, optarg);
				break;
			case 'w':
				if ((www = strdup(optarg)) == NULL)
					errx(1, "cannot allocate memory");

				break;
			case 'O':
				ret += ports_parse_options(pkg, optarg);
				break;
			case 'H':
				heuristic = true;
				break;
			case 'i':
				if ((input_path = strdup(optarg)) == NULL)
					errx(1, "cannot allocate memory");
				break;
			case 'l':
				legacy = true;
				break;
			default:
				printf("%c\n", ch);
				usage_register();
				return (-1);
		}
	}

	if (ret != 0) {
		pkg_error_warn("can not parse arguments");
		return (1);
	}

	if (plist == NULL)
		errx(EX_USAGE, "missing -f flag");

	for (unsigned int i = 0; required_flags[i].flag != NULL; i++)
		if (pkg_get(pkg, required_flags[i].attr) == NULL)
			errx(EX_USAGE, "missing %s flag", required_flags[i].flag);

	uname(&u);
	if (arch == NULL) {
		pkg_set(pkg, PKG_ARCH, u.machine);
	} else {
		pkg_set(pkg, PKG_ARCH, arch);
		free(arch);
	}

	/* if www is not given then try to determine it from description */
	if (www == NULL) {
		desc = pkg_get(pkg, PKG_DESC);
		regcomp(&preg, "^WWW:[:space:]*(.*)$", REG_EXTENDED|REG_ICASE|REG_NEWLINE);
		if (regexec(&preg, desc, 2, pmatch, 0) == 0) {
			size = pmatch[1].rm_eo - pmatch[1].rm_so;
			www = strndup(&desc[pmatch[1].rm_so], size);
			pkg_set(pkg, PKG_WWW, www);
			free(www);
		} else {
			pkg_set(pkg, PKG_WWW, "UNKNOWN");
		}
		regfree(&preg);
	} else {
		pkg_set(pkg, PKG_WWW, www);
		free(www);
	}

	if (strstr(u.release, "RELEASE") == NULL) {
		asprintf(&v, "%s-%d", u.release, __FreeBSD_version);
		pkg_set(pkg, PKG_OSVERSION, v);
		free(v);
	} else {
		pkg_set(pkg, PKG_OSVERSION, u.release);
	}
	/* TODO: missing osversion get it from uname*/

	ret += ports_parse_plist(pkg, plist);

	if (ret != 0) {
		pkg_error_warn("can not parse plist file");
		return (-1);
	}

	if (plist != NULL)
		free(plist);

	if (pkgdb_open(&db, PKGDB_DEFAULT, R_OK|W_OK) != EPKG_OK) {
		pkg_error_warn("can not open database");
		return (-1);
	}

	if (heuristic)
		pkg_analyse_files(db, pkg);

	if (input_path != NULL) {
		pkg_copy_tree(pkg, input_path, "/");
		free(input_path);
	}

	if (pkgdb_register_pkg(db, pkg) != EPKG_OK) {
		pkg_error_warn("can not register package");
		retcode = 1;
	}

	pkgdb_register_finale(db, ret);
	if (ret != EPKG_OK) {
		pkg_error_warn("can not register package");
		retcode = 1;
	}

	if (pkg_get(pkg, PKG_MESSAGE) != NULL && !legacy)
		printf("%s\n", pkg_get(pkg, PKG_MESSAGE));

	pkgdb_close(db);
	pkg_free(pkg);

	return (retcode);
}
