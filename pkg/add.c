/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

static int
is_url(const char * const pattern)
{
	if (strncmp(pattern, "http://", 7) == 0 ||
		strncmp(pattern, "https://", 8) == 0 ||
		strncmp(pattern, "ftp://", 6) == 0 ||
		strncmp(pattern, "file://", 7) == 0)
		return (EPKG_OK);

	return (EPKG_FATAL);
}

void
usage_add(void)
{
	fprintf(stderr, "usage: pkg add <pkg-name>\n");
	fprintf(stderr, "       pkg add <protocol>://<path>/<pkg-name>\n\n");
	fprintf(stderr, "For more information see 'pkg help add'.\n");
}

int
exec_add(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct sbuf *failedpkgs = NULL;
	char path[MAXPATHLEN + 1];
	char *file;
	int retcode = EPKG_OK;
	int i;
	int failedpkgcount = 0;
	struct pkg *p = NULL;

	if (argc < 2) {
		usage_add();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("Adding packages can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	failedpkgs = sbuf_new_auto();
	for (i = 1; i < argc; i++) {
		if (is_url(argv[i]) == EPKG_OK) {
			snprintf(path, sizeof(path), "./%s", basename(argv[i]));
			if ((retcode = pkg_fetch_file(argv[i], path, 0)) != EPKG_OK)
				break;

			file = path;
		} else {
			file = argv[i];
			if (access(file, F_OK) != 0) {
				warn("%s",file);
				if (errno == ENOENT)
					warnx("Did you mean 'pkg install %s'?", file);
				sbuf_cat(failedpkgs, argv[i]);
				if (i != argc - 1)
					sbuf_printf(failedpkgs, ", ");
				failedpkgcount++;
				continue;
			}

		}
			
		pkg_open(&p, file, NULL);

		if ((retcode = pkg_add(db, file, 0)) != EPKG_OK) {
			sbuf_cat(failedpkgs, argv[i]);
			if (i != argc - 1)
				sbuf_printf(failedpkgs, ", ");
			failedpkgcount++;
		}

	}

	pkgdb_close(db);
	
	if(failedpkgcount > 0) {
		sbuf_finish(failedpkgs);
		printf("\nFailed to install the following %d package(s): %s\n", failedpkgcount, sbuf_data(failedpkgs));
	}
	sbuf_delete(failedpkgs);

	if (messages != NULL) {
		sbuf_finish(messages);
		printf("%s", sbuf_data(messages));
	}

	return (retcode == EPKG_OK ? EX_OK : EX_SOFTWARE);
}

