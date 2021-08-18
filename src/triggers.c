/*-
 * Copyright (c) 2021 Baptiste Daroussin <bapt@FreeBSD.org>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <getopt.h>
#include <pkg.h>

#include "pkgcli.h"

void
usage_triggers(void)
{
	fprintf(stderr, "Usage: pkg triggers [-q]\n\n");
	fprintf(stderr, "For more information see 'pkg help triggers'.\n");
}

int
exec_triggers(int argc, char **argv)
{
	int ch;

	struct option longopts[] = {
		{ "quiet",	no_argument,	NULL,	'q' },
		{ NULL,		0,		NULL,	0   },
	};
	
	while ((ch = getopt_long(argc, argv, "+q", longopts, NULL)) != -1) {
                switch (ch) {
		case 'q':
			quiet = true;
			break;
		default:
			usage_triggers();
			return (EXIT_FAILURE);
		}
	}
	// argv += optind;

	pkg_execute_deferred_triggers();

	return (EXIT_SUCCESS);
}
