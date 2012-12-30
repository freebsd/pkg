/*
 * Copyright (c) 2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/types.h>
#include <sys/param.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <err.h>

#include <pkg.h>
#include "pkgcli.h"

/* -i stagedir -l legacy -f tmpplist -m metadir */

struct pkg *pkg;

static void
usage() {
	fprintf(stderr, "usage: %s register [-ld] [-i <input-path>]"
	    " -m <metadatadir> -f <plist-file>\n", getprogname());
	fprintf(stderr, "usage: %s create [-n] [-f format] [-o outdir] "
	    "[-p plist] [-r rootdir] -m manifestdir\n", getprogname());

	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	const char *cmd;

	if (argc < 2)
		usage();

	cmd = argv[1];
	argc--;
	argv++;

	if (strcmp(cmd, "register") == 0)
		return (exec_register(argc, argv));
	else if (strcmp(cmd, "create") == 0)
		return (exec_create(argc, argv));

	usage();
	/* NOT REACHED */
	return (EXIT_FAILURE);
}
