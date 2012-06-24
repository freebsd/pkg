/*
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <ctype.h>

#include "pkgcli.h"

void
usage_shlib(void)
{
	fprintf(stderr, "usage: pkg shlib <library>\n\n");
	fprintf(stderr, "<library> should be a filename without leading path.\n");
	fprintf(stderr, "For more information see 'pkg help shlib'.\n");
}

char*
sanitize(char *target, const char *source, size_t size)
{
	size_t i;
	int s;
	char *rc = target;

	for (i = 0; i < size - 1; i++) {
		s = source[i];
		if (s == '\0')
			break;
		if (isascii(s) && (isspace(s) || s == '/')) {
			rc = NULL;
			break;
		} else {
			target[i] = s;
		}
	}
	target[i] = '\0';

	return (rc);
}

int
exec_shlib(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	char libname[MAXPATHLEN + 1];
	int ret = EPKG_OK, retcode = EPKG_OK, count = 0;
	const char *name, *version;

	if (argc != 2) {
		usage_shlib();
		return (EX_USAGE);
	}

	if (sanitize(libname, argv[1], sizeof(libname)) == NULL) {
		usage_shlib();
		return (EX_USAGE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	if ((it = pkgdb_query_shlib(db, libname)) == NULL) {
		return (EX_IOERR);
	}

	while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		if (count == 0)
			printf("%s is linked to by the folowing packages:\n", libname);
		count++;
		pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version);
		printf("%s-%s\n", name, version);
	}

        if (ret != EPKG_END) {
		retcode = EPKG_WARN;
	} else if (count == 0) {
		printf("%s was not found in the database.\n", libname);
		retcode = EPKG_WARN;
	}
		
	pkg_free(pkg);
	pkgdb_it_free(it);

	pkgdb_close(db);
	return (retcode);
}
