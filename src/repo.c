/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <bsd_compat.h>
#include <getopt.h>
#include <signal.h>
#include <sysexits.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_BSD_READPASSPHRASE_H
#include <bsd/readpassphrase.h>
#else
#include <readpassphrase.h>
#endif

#include <unistd.h>

#include <pkg.h>
#include "pkgcli.h"

void
usage_repo(void)
{
	fprintf(stderr, "Usage: pkg repo [-lqL] [-o output-dir] <repo-path> "
	    "[<rsa-key>|signing_command: <the command>]\n\n");
	fprintf(stderr, "For more information see 'pkg help repo'.\n");
}

static int
password_cb(char *buf, int size, int rwflag, void *key)
{
	int len = 0;
	char pass[BUFSIZ];
	sigset_t sig, oldsig;

	(void)rwflag;
	(void)key;

	/* Block sigalarm temporary */
	sigemptyset(&sig);
	sigaddset(&sig, SIGALRM);
	sigprocmask(SIG_BLOCK, &sig, &oldsig);

	if (readpassphrase("\nEnter passphrase: ", pass, BUFSIZ, RPP_ECHO_OFF) == NULL)
		return 0;

	len = strlen(pass);

	if (len <= 0)  return 0;
	if (len > size) len = size;

	memset(buf, '\0', size);
	memcpy(buf, pass, len);
	memset(pass, 0, BUFSIZ);

	sigprocmask(SIG_SETMASK, &oldsig, NULL);

	return (len);
}

int
exec_repo(int argc, char **argv)
{
	int	 ret;
	int	 ch;
	bool	 filelist = false;
	char	*output_dir = NULL;
	char	*meta_file = NULL;
	bool	legacy = false;

	struct option longopts[] = {
		{ "list-files", no_argument,		NULL,	'l' },
		{ "output-dir", required_argument,	NULL,	'o' },
		{ "quiet",	no_argument,		NULL,	'q' },
		{ "meta-file",	required_argument,	NULL,	'm' },
		{ "legacy",	no_argument,	NULL,	'L' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+lo:qm:L", longopts, NULL)) != -1) {
		switch (ch) {
		case 'l':
			filelist = true;
			break;
		case 'o':
			output_dir = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 'm':
			meta_file = optarg;
			break;
		case 'L':
			legacy = true;
			break;
		default:
			usage_repo();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage_repo();
		return (EX_USAGE);
	}

	if (argc > 2 && strcmp(argv[1], "signing_command:") != 0) {
		usage_repo();
		return (EX_USAGE);
	}

	if (output_dir == NULL)
		output_dir = argv[0];

	ret = pkg_create_repo(argv[0], output_dir, filelist, meta_file, legacy);

	if (ret != EPKG_OK) {
		printf("Cannot create repository catalogue\n");
		return (EX_IOERR);
	}

	if (pkg_finish_repo(output_dir, password_cb, argv + 1, argc - 1,
	    filelist) != EPKG_OK)
		return (EX_DATAERR);

	return (EX_OK);
}
