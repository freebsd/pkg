#include <sys/stat.h>
#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/utsname.h>
#include <regex.h>

#include "register.h"

static void compute_flatsize(struct pkg *pkg);

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
	struct utsname u;

	regex_t preg;
	regmatch_t pmatch[2];

	char ch;
	char *plist = NULL;
	char *v = NULL;
	char *arch = NULL;
	char *www = NULL;

	const char *desc = NULL;
	size_t size;

	bool heuristic = false;

	int retcode = 0;
	int ret = 0;

	pkg_new(&pkg);
	while ((ch = getopt(argc, argv, "vHc:d:f:p:P:m:o:C:n:M:s:a:r:w:O:")) != -1) {
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
				pkg_set(pkg, PKG_PREFIX, optarg);
				break;
			case 'P':
				ret += ports_parse_depends(pkg, optarg);
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
				ret += ports_parse_conflicts(pkg, optarg);
				break;
			case 'M':
				ret += pkg_set_from_file(pkg, PKG_MESSAGE, optarg);
				break;
			case 's':
				ret += ports_parse_scripts(pkg, optarg);
				break;
			case 'a':
				arch = strdup(optarg);
				break;
			case 'r': /* responsible */
				pkg_set(pkg, PKG_MAINTAINER, optarg);
				break;
			case 'w':
				www = strdup(optarg);
				break;
			case 'O':
				ret += ports_parse_options(pkg, optarg);
				break;
			case 'H':
				heuristic = true;
				break;
			default:
				printf("%c\n", ch);
				usage_register();
				return (-1);
		}
	}

	uname(&u);
	if (arch == NULL) {
		pkg_set(pkg, PKG_ARCH, u.machine);
	} else {
		pkg_set(pkg, PKG_ARCH, arch);
		free(arch);
	}

	/* is www is not given then try to determine it from description */
	if (www == NULL) {
		desc = pkg_get(pkg, PKG_DESC);
		regcomp(&preg, "^WWW:[:space:]*(.*)$", REG_EXTENDED|REG_ICASE|REG_NEWLINE);
		if (regexec(&preg, desc, 2, pmatch, 0) == 0) {
			size = pmatch[1].rm_eo - pmatch[1].rm_so;
			www = strndup(&desc[pmatch[1].rm_so], size);
			pkg_set(pkg, PKG_WWW, www);
			free(www);
		}
		regfree(&preg);
	} else {
		pkg_set(pkg, PKG_WWW, www);
		free(www);
	}


	if (strstr(u.release, "RELEASE") == NULL) {
		v = malloc(strlen(u.release) + 10); /* 10 should be enough */
		snprintf(v, strlen(u.release) + 10, "%s-%d", u.release, __FreeBSD_version);
		pkg_set(pkg, PKG_OSVERSION, v);
		free(v);
	} else {
		pkg_set(pkg, PKG_OSVERSION, u.release);
	}
	pkg_set(pkg, PKG_ARCH, optarg);
	/* TODO: missing osversion get it from uname*/

	if (ret < 0) {
		pkg_free(pkg);
		return (ret);
	}

	ret += ports_parse_plist(pkg, plist);

	if (ret < 0)
		return (ret);

	if (plist != NULL)
		free(plist);

	if (pkgdb_open(&db) != EPKG_OK) {
		pkg_error_warn("can not open database");
		return (-1);
	}

	if (heuristic)
		pkg_analyse_files(db, pkg);

	compute_flatsize(pkg);

	if (pkgdb_register_pkg(db, pkg) != EPKG_OK) {
		pkg_error_warn("can not register package");
		retcode = 1;
	}

	pkgdb_close(db);
	pkg_free(pkg);

	return (retcode);
}

static void
compute_flatsize(struct pkg *pkg)
{
	struct pkg_file **files;
	struct stat st;
	int64_t size = 0;

	files = pkg_files(pkg);
	for (int i = 0; files[i] != NULL; i++) {
		if (stat(pkg_file_path(files[i]), &st) != 0) {
			warn("stat(%s)", pkg_file_path(files[i]));
			continue;
		}
		size += st.st_size;
	}

	pkg_setflatsize(pkg, size);
}
