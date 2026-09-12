/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
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

#include <bsd_compat.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>
#include "pkgcli.h"

void
usage_repo(void)
{
	fprintf(stderr, "Usage: pkg repo [-hlqs] [-m metafile] [-o output-dir] "
                        "[-t <keytype> [-k keyfile | -c cmd]] <repo-path>\n\n");
	fprintf(stderr, "For more information see 'pkg help repo'.\n");
}

int
exec_repo(int argc, char **argv)
{
	int	 ch, i;
	bool	 hash = false;
	bool	 hash_symlink = false;
	bool	 got_sign_cmd = false;
	bool	 got_sign_key = false;
	bool	 got_signopts = false;
	struct pkg_repo_create *prc = pkg_repo_create_new();
	char	*cpos;
	sb_t	 cmd = sb_init();

	hash = (getenv("PKG_REPO_HASH") != NULL);
	hash_symlink = (getenv("PKG_REPO_SYMLINK") != NULL);

	struct option longopts[] = {
		{ "sign-cmd",	required_argument,	NULL,	'c' },
		{ "groups",	required_argument,	NULL,	'g' },
		{ "hash",	no_argument,		NULL,	'h' },
		{ "sign-key",	required_argument,	NULL,	'k' },
		{ "list-files", no_argument,		NULL,	'l' },
		{ "meta-file",	required_argument,	NULL,	'm' },
		{ "output-dir", required_argument,	NULL,	'o' },
		{ "quiet",	no_argument,		NULL,	'q' },
		{ "symlink",	no_argument,		NULL,	's' },
		{ "sign-type",	required_argument,	NULL,	't' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+c:g:hk:lm:o:qst:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'c':
			got_sign_cmd = got_signopts = true;
			pkg_repo_create_set_signcmd(prc, optarg);
			break;
		case 'g':
			pkg_repo_create_set_groups(prc, optarg);
			break;
		case 'h':
			hash = true;
			break;
		case 'k':
			got_sign_key = got_signopts = true;
			pkg_repo_create_set_signkey(prc, optarg);
			break;
		case 'l':
			pkg_repo_create_set_create_filelist(prc, true);
			break;
		case 'm':
			pkg_repo_create_set_metafile(prc, optarg);
			break;
		case 'o':
			pkg_repo_create_set_output_dir(prc, optarg);
			break;
		case 'q':
			quiet = true;
			break;
		case 's':
			hash_symlink = true;
			break;
		case 't':
			got_signopts = true;
			pkg_repo_create_set_signtype(prc, optarg);
			break;
		default:
			usage_repo();
			return (EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		pkg_repo_create_free(prc);
		usage_repo();
		return (EXIT_FAILURE);
	}

	pkg_repo_create_set_hash(prc, hash);
	pkg_repo_create_set_hash_symlink(prc, hash_symlink);
	pkg_repo_create_set_password_cb(prc, password_cb);

	/* If we have signing options and we have signing args, error. */
	if (argc > 1 && got_signopts) {
		pkg_repo_create_free(prc);
		warnx("Cannot pass both legacy signing arguments and modern signing options.");
		usage_repo();
		return (EXIT_FAILURE);
	}

	if (got_sign_cmd && got_sign_key) {
		pkg_repo_create_free(prc);
		warnx("Cannot pass both signing key and signing command.");
		usage_repo();
		return (EXIT_FAILURE);
	}

	/* Compatibility with old signing calling convention. */
	if (argc == 2) {
		/* Should be "dir rsa:path/to/key.pem" */
		if ((cpos = strchr(argv[1], ':')) == NULL) {
			/* No type detected, assume "rsa" */
			pkg_repo_create_set_signtype(prc, "rsa");
			pkg_repo_create_set_signkey(prc, argv[1]);
		} else {
			cpos[0] = '\0';
			pkg_repo_create_set_signtype(prc, argv[1]);
			pkg_repo_create_set_signkey(prc, cpos + 1);
		}
	} else if (argc > 2) {
		/* Should be "dir signing_command: sign.sh arg arg" */
		if (STREQ(argv[1], "signing_command:")) {
			for (i = 2; i < argc; i++) {
				if (strspn(argv[i], " \t\n") > 0)
					sb_printf(&cmd, " \"%s\" ", argv[i]);
				else
					sb_printf(&cmd, " %s ", argv[i]);
			}
			pkg_repo_create_set_signcmd(prc, sb_str(&cmd));
		} else {
			sb_fini(&cmd);
			pkg_repo_create_free(prc);
			usage_repo();
			return (EXIT_FAILURE);
		}
	}

	if (pkg_repo_create(prc, argv[0]) != EPKG_OK) {
		printf("Cannot create repository catalogue\n");
		sb_fini(&cmd);
		pkg_repo_create_free(prc);
		return (EXIT_FAILURE);
	}

	sb_fini(&cmd);
	pkg_repo_create_free(prc);
	return (EXIT_SUCCESS);
}
