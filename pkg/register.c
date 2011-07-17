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

static const char * const scripts[] = {
	"+INSTALL",
	"+PRE_INSTALL",
	"+POST_INSTALL",
	"+POST_INSTALL",
	"+DEINSTALL",
	"+PRE_DEINSTALL",
	"+POST_DEINSTALL",
	"+UPGRADE",
	"+PRE_UPGRADE",
	"+POST_UPGRADE",
	"pkg-install",
	"pkg-pre-install",
	"pkg-post-install",
	"pkg-deinstall",
	"pkg-pre-deinstall",
	"pkg-post-deinstall",
	"pkg-upgrade",
	"pkg-pre-upgrade",
	"pkg-post-upgrade",
	NULL
};

void
usage_register(void)
{
	fprintf(stderr, "usage: pkg register [-l] -m <metadatadir> -f <plist_file>\n\n");
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
	char *mdir = NULL;
	char *www = NULL;
	char *input_path = NULL;
	char fpath[MAXPATHLEN];

	const char *desc = NULL;
	size_t size;

	bool heuristic = false;
	bool legacy = false;

	int i;
	int retcode = 0;
	int ret = 0;

	if (geteuid() != 0) {
		warnx("registering packages can only be done as root");
		return (EX_NOPERM);
	}

	pkg_new(&pkg, PKG_INSTALLED);
	while ((ch = getopt(argc, argv, "a:f:m:i:ld")) != -1) {
		switch (ch) {
			case 'f':
				if ((plist = strdup(optarg)) == NULL)
					errx(1, "cannot allocate memory");

				break;
			case 'm':
				mdir = strdup(optarg);
				break;
			case 'a':
				arch = strdup(optarg);
				break;
			case 'd':
				pkg_setautomatic(pkg);
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

	uname(&u);
	if (arch == NULL) {
		pkg_set(pkg, PKG_ARCH, u.machine);
	} else {
		pkg_set(pkg, PKG_ARCH, arch);
		free(arch);
	}

	if (mdir == NULL)
		errx(EX_USAGE, "missing -m flag");

	snprintf(fpath, MAXPATHLEN, "%s/+MANIFEST", mdir);
	if ((ret = pkg_load_manifest_file(pkg, fpath)) != EPKG_OK) {
		pkg_error_warn("can not parse manifest %s", fpath);
		return (EX_SOFTWARE);
	}

	snprintf(fpath, MAXPATHLEN, "%s/+DESC", mdir);
	if (pkg_set_from_file(pkg, PKG_DESC, fpath) != EPKG_OK)
		pkg_error_warn("");

	snprintf(fpath, MAXPATHLEN, "%s/+DISPLAY", mdir);
	if (access(fpath, F_OK) == 0 && pkg_set_from_file(pkg, PKG_MESSAGE, fpath) != EPKG_OK)
		pkg_error_warn("");

	snprintf(fpath, MAXPATHLEN, "%s/+MTREE_DIRS", mdir);
	if (access(fpath, F_OK) == 0 && pkg_set_from_file(pkg, PKG_MTREE, fpath) != EPKG_OK)
		pkg_error_warn("");

	for (i = 0; scripts[i] != NULL; i++) {
		snprintf(fpath, MAXPATHLEN, "%s/%s", mdir, scripts[i]);
		if (access(fpath, F_OK) == 0 && pkg_addscript_file(pkg, fpath) != EPKG_OK)
			pkg_error_warn("");
	}

	/* if www is not given then try to determine it from description */
	if (www == NULL) {
		desc = pkg_get(pkg, PKG_DESC);
		regcomp(&preg, "^WWW:[[:space:]]*(.*)$", REG_EXTENDED|REG_ICASE|REG_NEWLINE);
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

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
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
