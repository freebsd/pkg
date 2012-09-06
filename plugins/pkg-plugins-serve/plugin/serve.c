/*
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

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>
#include <mongoose.h>

#include "serve.h"

#define PLUGIN_NAME "serve"

static void plugin_serve_usage(void);

int
pkg_plugins_init_serve(void)
{
	if (pkg_plugins_register_cmd(PLUGIN_NAME, &plugin_serve_callback) != EPKG_OK) {
		fprintf(stderr, "Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkg_plugins_shutdown_serve(void)
{
	/* nothing to be done here */

	return (EPKG_OK);
}

static void
plugin_serve_usage(void)
{
	fprintf(stderr, "usage: pkg serve [-d <wwwroot>] [-p <port>]\n\n");
	fprintf(stderr, "A mongoose plugin for serving files\n");
}

int
plugin_serve_callback(int argc, char **argv)
{
	struct mg_context *ctx = NULL;
	struct stat st;
	const char *wwwroot = NULL;
	const char *port = NULL;
        int ch;

        while ((ch = getopt(argc, argv, "d:p:")) != -1) {
		switch (ch) {
		case 'd':
			wwwroot = optarg;
                        break;
                case 'p':
			port = optarg;
                        break;
                default:
                        plugin_serve_usage();
                        return (EX_USAGE);
                }
	}
	argc -= optind;
	argv += optind;

	/* default port to use is 8080 */
	if (port == NULL)
		port = "8080";

	if (wwwroot == NULL) {
		fprintf(stderr, ">>> You need to specify a directory for serving");
		return (EX_USAGE);
	}

	stat(wwwroot, &st);
	if (S_ISDIR(st.st_mode) == 0) {
		fprintf(stderr, ">>> '%s' is not a directory\n", wwwroot);
		return (EX_USAGE);
	}

	const char *options[] = {
		"listening_ports", port,
		"document_root", wwwroot,
		"enable_directory_listing", "yes",
		NULL, NULL
	};

	ctx = mg_start(NULL, NULL, options);

	printf(">>> Server listening on port %s\n", port);
	printf(">>> Serving directory %s\n", wwwroot);
	printf(">>> In order to stop the server press ENTER ...");
	
	getchar(); /* serve until user pressed enter */

	printf(">>> Shutting down server\n");
	
	mg_stop(ctx);

	printf(">>> Done\n");

	return (EPKG_OK);
}
