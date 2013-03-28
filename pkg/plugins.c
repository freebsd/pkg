/*-
 * Copyright (c) 2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

#include <stdio.h>
#include <stdbool.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_plugins(void)
{
        fprintf(stderr, "usage: pkg plugins [-l] <plugin>\n\n");
        fprintf(stderr, "For more information see 'pkg help plugins'.\n");
}

int
exec_plugins(__unused int argc, __unused char **argv)
{
	struct pkg_plugin *p = NULL;
	int ch;
	bool __unused list_only = true;

	while ((ch = getopt(argc, argv, "l")) != -1) {
                switch (ch) {
		case 'l':
			list_only = true;
			break;
                default:
                        usage_plugins();
                        return (EX_USAGE);
                }
        }
        argc -= optind;
        argv += optind;

	/**
	 * For now only display the available plugins
	 * @todo: implement enabling, disabling and configuring of plugins
	 */

	printf("%-10s %-45s %-10s\n", "NAME", "DESC", "VERSION");
	while (pkg_plugins(&p) != EPKG_END)
		printf("%-10s %-45s %-10s\n",
		       pkg_plugin_get(p, PKG_PLUGIN_NAME),
		       pkg_plugin_get(p, PKG_PLUGIN_DESC),
		       pkg_plugin_get(p, PKG_PLUGIN_VERSION));

	return (EX_OK);
}
