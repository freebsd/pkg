/*-
 * Copyright(c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <getopt.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_repositories(void)
{
        fprintf(stderr, "Usage: pkg repositories [-edl] [repository]\n\n");
}


typedef enum {
	REPO_SHOW_ALL = 0,
	REPO_SHOW_ENABLED = 1U << 0,
	REPO_SHOW_DISABLED = 1U << 1,
} repo_show_t;

int
exec_repositories(int argc, char **argv)
{
	const char *r = NULL;
	struct pkg_repo *repo = NULL;
	bool list_only = false;
	repo_show_t rs = REPO_SHOW_ALL;
	int ch;

	struct option longopts[] = {
		{ "list",	no_argument,	NULL,	'l' },
		{ "enabled",	no_argument,	NULL,	'e' },
		{ "disabled",	no_argument,	NULL,	'd' },
		{ NULL,		0,		NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+led", longopts, NULL)) != -1) {
                switch (ch) {
		case 'l':
			list_only = true;
			break;
		case 'e':
			rs |= REPO_SHOW_ENABLED;
			break;
		case 'd':
			rs |= REPO_SHOW_DISABLED;
			break;
		default:
			usage_repositories();
			return (EXIT_FAILURE);
		}
	}

	if (rs == REPO_SHOW_ALL)
		rs |= REPO_SHOW_DISABLED|REPO_SHOW_ENABLED;

	argc -= optind;
	argv += optind;

	if (argc == 1)
		r = argv[0];

	while (pkg_repos(&repo) == EPKG_OK) {
		if (r && !STREQ(r, pkg_repo_name(repo)))
			continue;
		if (pkg_repo_enabled(repo)) {
			if ((rs & REPO_SHOW_ENABLED) == 0)
				continue;
		} else {
			if ((rs & REPO_SHOW_DISABLED) == 0)
				continue;
		}
		if (list_only) {
			printf("%s\n", pkg_repo_name(repo));
			continue;
		}
		print_repository(repo, false);
	}


	return (EXIT_SUCCESS);
}
