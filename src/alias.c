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
#include <sys/sbuf.h>

#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <getopt.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_alias(void)
{
	fprintf(stderr, "Usage: pkg alias [alias]\n");
}

int
exec_alias(int argc, char **argv)
{
	const pkg_object *all_aliases;
	const pkg_object *alias;
	pkg_iter it = NULL;
	int found_alias = 0;

	if (argc > 2) {
		usage_alias();
		return(EX_USAGE);
	}

	all_aliases = pkg_config_get("ALIAS");

	if (argc == 2) {
		while ((alias = pkg_object_iterate(all_aliases, &it))) {
			if (strcmp(argv[1], pkg_object_key(alias)) == 0) {
				printf("\t%-15s -> '%s'\n", argv[1], pkg_object_string(alias));
				found_alias = 1;
				return (0);
			}
		}
	} else {
		printf("%-20s %-45s\n", "ALIAS", "ARGUMENTS");
		while ((alias = pkg_object_iterate(all_aliases, &it))) {
			printf("%-20s '%s'\n", pkg_object_key(alias), pkg_object_string(alias));
		}
	}

	if ((argc == 2) && (found_alias == 0)) {
		printf("No alias configured named '%s'\n", argv[1]);
		return(EX_USAGE);
	}

	return (0);
}

