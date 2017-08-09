/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2017 David Demelier <markand@malikania.fr>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <err.h>
#include <getopt.h>
#include <pkg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sysexits.h>

#include "pkgcli.h"

static void
print_repo_list(bool list_disabled)
{
	struct pkg_repo *repo = NULL;

	while (pkg_repos(&repo) == EPKG_OK) {
		if (!pkg_repo_enabled(repo) && !list_disabled)
			continue;

		printf("%s\n", pkg_repo_name(repo));
	}
}

static void
print_repo_info(const char *arg)
{
	struct pkg_repo *repo = NULL;
	const char *key, *fingerprints;

	if ((repo = pkg_repo_find(arg)) == NULL)
		errx(EX_USAGE, "Repository %s not found", arg);

	printf("%-15s: ", "Name");
	printf("%s\n", pkg_repo_name(repo));
	printf("%-15s: ", "URL");
	printf("%s\n", pkg_repo_url(repo));
	printf("%-15s: ", "Enabled");
	printf("%s\n", pkg_repo_enabled(repo) ? "true" : "false");

	printf("%-15s: ", "Mirror type");
	switch (pkg_repo_mirror_type(repo)) {
	case SRV:
		puts("srv");
		break;
	case HTTP:
		puts("http");
		break;
	default:
		puts("none");
	}

	printf("%-15s: ", "Signature");
	switch (pkg_repo_signature_type(repo)) {
	case SIG_PUBKEY:
		puts("pubkey");
		break;
	case SIG_FINGERPRINT:
		puts("fingerprint");
		break;
	default:
		puts("none");
	}

	printf("%-15s: ", "Key");
	if ((key = pkg_repo_key(repo)) == NULL)
		key = "none";
	puts(key);

	printf("%-15s: ", "Fingerprints");
	if ((fingerprints = pkg_repo_fingerprints(repo)) == NULL)
		fingerprints = "none";
	puts(fingerprints);
}

void
usage_repos(void)
{
	fprintf(stderr, "Usage: pkg repos [-a]\n");
	fprintf(stderr, "       pkg repos repo-name\n\n");
	fprintf(stderr, "For more information see 'pkg help repos'.\n");
}

int
exec_repos(int argc, char **argv)
{
	int ch;
	bool list_disabled = false;

	struct option longopts[] = {
		{ "all",		no_argument,		NULL,	'a' },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "a", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			list_disabled = true;
			break;
		default:
			usage_repos();
			return(EX_USAGE);
		}
	};

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		print_repo_list(list_disabled);
	} else {
		print_repo_info(argv[0]);
	}

	return 0;
}
