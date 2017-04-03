/*-
 * Copyright (c) 2015 Lars Engels <lme@FreeBSD.org>
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
#include <getopt.h>
#include <utstring.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_alias(void)
{
	fprintf(stderr, "Usage: pkg alias [-ql] [alias]\n\n");
	fprintf(stderr, "For more information see 'pkg help alias'.\n");
}

int
exec_alias(int argc, char **argv)
{
	const pkg_object *all_aliases;
	const pkg_object *alias;
	pkg_iter it = NULL;
	int ch;
	int ret = EX_OK;
	bool list = false;

	struct option longopts[] = {
		{ "quiet",	no_argument,	NULL, 'q' },
		{ "list",	no_argument,	NULL, 'l' },
		{ NULL,		0,		NULL, 0 },
	};

	while ((ch = getopt_long(argc, argv, "+ql", longopts, NULL)) != -1) {
		switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 'l':
			list = true;
			break;
		default:
			usage_alias();
			return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	all_aliases = pkg_config_get("ALIAS");

	if (argc == 0) {
		if (!quiet && list)
			printf("%s\n", "ALIAS");
		else if (!quiet)
			printf("%-20s %s\n", "ALIAS", "ARGUMENTS");
		while ((alias = pkg_object_iterate(all_aliases, &it))) {
			if (list)
				printf("%s\n", pkg_object_key(alias));
			else
				printf("%-20s '%s'\n", pkg_object_key(alias), pkg_object_string(alias));
		}
		return (ret);
	}

	for (int i = 0; i < argc; i++) {
		it = NULL;
		while ((alias = pkg_object_iterate(all_aliases, &it))) {
			if (strcmp(argv[i], pkg_object_key(alias)) == 0)
				break;
		}
		if (alias) {
			if (list)
				printf("%s\n", argv[i]);
			else
				printf("%-20s '%s'\n", argv[i], pkg_object_string(alias));
		} else {
			warnx("No such alias: '%s'", argv[i]);
			ret = EX_UNAVAILABLE;
		}
	}

	return (ret);
}

