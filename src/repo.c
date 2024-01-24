/*-
 * Copyright (c) 2011-2024 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <bsd_compat.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_READPASSPHRASE_H
#include <readpassphrase.h>
#elif defined(HAVE_BSD_READPASSPHRASE_H)
#include <bsd/readpassphrase.h>
#else
#include "readpassphrase_compat.h"
#endif

#include <unistd.h>

#include <pkg.h>
#include "pkgcli.h"

void
usage_repo(void)
{
	fprintf(stderr, "Usage: pkg repo [-hlqs] [-m metafile] [-o output-dir] <repo-path> "
	    "[rsa:<rsa-key>|signing_command: <the command>]\n\n");
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
	int	 ch;
	bool	 hash = false;
	bool	 hash_symlink = false;
	struct pkg_repo_create *prc = pkg_repo_create_new();

	hash = (getenv("PKG_REPO_HASH") != NULL);
	hash_symlink = (getenv("PKG_REPO_SYMLINK") != NULL);

	struct option longopts[] = {
		{ "hash",	no_argument,		NULL,	'h' },
		{ "list-files", no_argument,		NULL,	'l' },
		{ "meta-file",	required_argument,	NULL,	'm' },
		{ "output-dir", required_argument,	NULL,	'o' },
		{ "quiet",	no_argument,		NULL,	'q' },
		{ "symlink",	no_argument,		NULL,	's' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+hlo:qm:s", longopts, NULL)) != -1) {
		switch (ch) {
		case 'h':
			hash = true;
			break;
		case 'l':
			pkg_repo_create_set_create_filelist(prc, true);
			break;
		case 'o':
			pkg_repo_create_set_output_dir(prc, optarg);
			break;
		case 'q':
			quiet = true;
			break;
		case 'm':
			pkg_repo_create_set_metafile(prc, optarg);
			break;
		case 's':
			hash_symlink = true;
			break;
		default:
			usage_repo();
			return (EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	pkg_repo_create_set_hash(prc, hash);
	pkg_repo_create_set_hash_symlink(prc, hash_symlink);
	pkg_repo_create_set_sign(prc, argv + 1, argc - 1, password_cb);

	if (argc < 1) {
		usage_repo();
		return (EXIT_FAILURE);
	}

	if (argc > 2 && strcmp(argv[1], "signing_command:") != 0) {
		usage_repo();
		return (EXIT_FAILURE);
	}

	if (pkg_repo_create(prc, argv[0]) != EPKG_OK) {
		printf("Cannot create repository catalogue\n");
		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}
