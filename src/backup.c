/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
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
#include <getopt.h>
#include <sysexits.h>
#include <unistd.h>

#include "pkgcli.h"

void
usage_backup(void)
{
	fprintf(stderr, "Usage: pkg backup -d <dest_file>\n");
	fprintf(stderr, "       pkg backup -r <src_file>\n\n");
	fprintf(stderr, "For more information see 'pkg help backup'.\n");
}

int
exec_backup(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	char		*backup_file = NULL;
	bool		 dump = false;
	bool		 restore = false;
	int		 ch;

	struct option longopts[] = {
		{ "dump",	required_argument,	NULL,	'd' },
		{ "restore",	required_argument,	NULL,	'r' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "d:r:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'd':
			dump = true;
			backup_file = optarg;
			break;
		case 'r':
			restore = true;
			backup_file = optarg;
			break;
		default:
			usage_backup();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if ( dump == restore ) {
		usage_backup();
		return (EX_USAGE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (dump) {
		if (isatty(fileno(stdin)))
				printf("Dumping database...\n");
		if (pkgdb_dump(db, backup_file) == EPKG_FATAL)
			return (EX_IOERR);

		if (isatty(fileno(stdin)))
			printf("done\n");
	}

	if (restore) {
		if (isatty(fileno(stdin)))
			printf("Restoring database...\n");
		if (pkgdb_load(db, backup_file) == EPKG_FATAL)
			return (EX_IOERR);
		if (isatty(fileno(stdin)))
			printf("done\n");
	}

	pkgdb_close(db);

	return (EX_OK);
}
