/*
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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

#include <sys/stat.h>

#include <err.h>
#include <fts.h>
#include <pkg.h>
#include <stdbool.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pkgcli.h"

void
usage_clean(void)
{
	fprintf(stderr, "usage: pkg clean\n\n");
	fprintf(stderr, "For more information see 'pkg help clean'.\n");
}

int
exec_clean(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	struct pkg *p = NULL;
	FTS *fts = NULL;
	FTSENT *ent = NULL;
	const char *cachedir;
	char *paths[2];
	char *repopath;
	bool to_delete;
	int retcode = EX_SOFTWARE;
	int ret;

	(void)argc;
	(void)argv;

	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK) {
		warnx("Cannot get cachedir config entry");
		return 1;
	}

	paths[0] = __DECONST(char*, cachedir);
	paths[1] = NULL;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		goto cleanup;
	}

	if ((fts = fts_open(paths, FTS_PHYSICAL, NULL)) == NULL) {
		warn("fts_open(%s)", cachedir);
		goto cleanup;
	}

	while ((ent = fts_read(fts)) != NULL) {
		const char *origin, *pkgrepopath;

		if (ent->fts_info != FTS_F)
			continue;

		repopath = ent->fts_path + strlen(cachedir);
		if (repopath[0] == '/')
			repopath++;

		if (pkg_open(&pkg, ent->fts_path) != EPKG_OK) {
			warnx("skipping %s", ent->fts_path);
			continue;
		}

		pkg_get(pkg, PKG_ORIGIN, &origin);
		it = pkgdb_search(db, origin, MATCH_EXACT, FIELD_ORIGIN,
		    FIELD_NONE, NULL);

		if (it == NULL) {
			warnx("skipping %s", ent->fts_path);
			continue;
		}

		if ((ret = pkgdb_it_next(it, &p, PKG_LOAD_BASIC)) ==
		    EPKG_FATAL) {
			warnx("skipping %s", ent->fts_path);
			continue;
		}
		to_delete = false;
		pkg_get(p, PKG_REPOPATH, &pkgrepopath);
		if (ret == EPKG_END) {
			to_delete = true;
			printf("%s does not exist anymore, deleting it\n",
			    repopath);
		} else if (strcmp(repopath, pkgrepopath)) {
			printf("%s is out-of-date, deleting it\n", repopath);
			to_delete = true;
		}

		if (to_delete == true) {
			if (unlink(ent->fts_path) != 0)
				warn("unlink(%s)", ent->fts_path);
		}

		pkgdb_it_free(it);
	}

	retcode = EX_OK;

	cleanup:
	if (pkg != NULL)
		pkg_free(pkg);
	if (p != NULL)
		pkg_free(p);
	if (fts != NULL)
		fts_close(fts);
	if (db != NULL)
		pkgdb_close(db);

	return (retcode);
}
