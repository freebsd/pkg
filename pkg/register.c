/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <regex.h>

#include "pkgcli.h"

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
	fprintf(stderr, "usage: pkg register [-ld] [-a <arch>] [-i <input-path>] -m <metadatadir> -f <plist-file>\n\n");
	fprintf(stderr, "For more information see 'pkg help register'.\n");
}

int
exec_register(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb *db = NULL;

	regex_t preg;
	regmatch_t pmatch[2];

	int ch;
	char *plist = NULL;
	char *arch = NULL;
	char myarch[BUFSIZ];
	char *mdir = NULL;
	char *www = NULL;
	char *input_path = NULL;
	char fpath[MAXPATHLEN + 1];

	const char *message;
	const char *desc = NULL;
	size_t size;

	bool heuristic = false;
	bool legacy = false;

	int i;
	int ret = EPKG_OK, retcode = EPKG_OK;

	if (geteuid() != 0) {
		warnx("registering packages can only be done as root");
		return (EX_NOPERM);
	}

	pkg_new(&pkg, PKG_INSTALLED);
	while ((ch = getopt(argc, argv, "a:f:m:i:ld")) != -1) {
		switch (ch) {
			case 'f':
				if ((plist = strdup(optarg)) == NULL)
					err(1, "cannot allocate memory");

				break;
			case 'm':
				if ((mdir = strdup(optarg)) == NULL)
					err(1, "cannot allocate memory");
				break;
			case 'a':
				if ((arch = strdup(optarg)) == NULL)
					err(1, "cannot allocate memory");
				break;
			case 'd':
				pkg_set(pkg, PKG_AUTOMATIC, true);
				break;
			case 'i':
				if ((input_path = strdup(optarg)) == NULL)
					err(1, "cannot allocate memory");
				break;
			case 'l':
				legacy = true;
				break;
			default:
				printf("%c\n", ch);
				usage_register();
				return (EX_USAGE);
		}
	}

	if (plist == NULL)
		errx(EX_USAGE, "missing -f flag");

	if (arch == NULL) {
		pkg_get_myarch(myarch, BUFSIZ);
		pkg_set(pkg, PKG_ARCH, myarch);
	} else {
		pkg_set(pkg, PKG_ARCH, arch);
		free(arch);
	}

	if (mdir == NULL)
		errx(EX_USAGE, "missing -m flag");

	snprintf(fpath, sizeof(fpath), "%s/+MANIFEST", mdir);
	if ((ret = pkg_load_manifest_file(pkg, fpath)) != EPKG_OK) {
		return (EX_IOERR);
	}

	snprintf(fpath, sizeof(fpath), "%s/+DESC", mdir);
	pkg_set_from_file(pkg, PKG_DESC, fpath);

	snprintf(fpath, sizeof(fpath), "%s/+DISPLAY", mdir);
	if (access(fpath, F_OK) == 0)
		 pkg_set_from_file(pkg, PKG_MESSAGE, fpath);

	snprintf(fpath, sizeof(fpath), "%s/+MTREE_DIRS", mdir);
	if (access(fpath, F_OK) == 0)
		pkg_set_from_file(pkg, PKG_MTREE, fpath);

	for (i = 0; scripts[i] != NULL; i++) {
		snprintf(fpath, sizeof(fpath), "%s/%s", mdir, scripts[i]);
		if (access(fpath, F_OK) == 0)
			pkg_addscript_file(pkg, fpath);
	}

	/* if www is not given then try to determine it from description */
	if (www == NULL) {
		pkg_get(pkg, PKG_DESC, &desc);
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

	ret += ports_parse_plist(pkg, plist);

	if (ret != EPKG_OK) {
		return (EX_IOERR);
	}

	if (plist != NULL)
		free(plist);

	if (pkg_register_shlibs(pkg) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (heuristic)
		pkg_analyse_files(db, pkg);

	if (input_path != NULL) {
		pkg_copy_tree(pkg, input_path, "/");
		free(input_path);
	}

	retcode = pkgdb_register_pkg(db, pkg, 0);
	pkgdb_register_finale(db, retcode);

	pkg_get(pkg, PKG_MESSAGE, &message);
	if (message != NULL && !legacy)
		printf("%s\n", message);

	pkgdb_close(db);
	pkg_free(pkg);

	return (retcode);
}
