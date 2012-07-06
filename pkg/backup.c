/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <pkg.h>
#include <sysexits.h>

#include "pkgcli.h"

void
usage_backup(void)
{
	fprintf(stderr, "usage: pkg backup -d <dest_file>\n");
	fprintf(stderr, "       pkg backup -r <src_file>\n\n");
	fprintf(stderr, "For more information see 'pkg help backup'.\n");
}

int
exec_backup(int argc, char **argv)
{
	struct pkgdb  *db = NULL;
	char *dest = NULL;

	if (argc < 2 || argc > 3 || argv[1][0] != '-') {
		usage_backup();
		return (EX_USAGE);
	}

	if (argc == 3)
		dest = argv[2];

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (argv[1][1] == 'd') {
		printf("Dumping database...");
		if (pkgdb_dump(db, dest) == EPKG_FATAL)
			return (EX_IOERR);

		printf(" done\n");
	}

	if (argv[1][1] == 'r') {
		printf("Restoring database...");
		if (pkgdb_load(db, dest) == EPKG_FATAL)
			return (EX_IOERR);
		printf(" done\n");
	}

	pkgdb_close(db);

	return (EX_OK);
}
