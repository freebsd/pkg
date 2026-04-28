/*-
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>
#include <pkg/checksum.h>
#include "pkgcli.h"

void
usage_checksum(void)
{
	fprintf(stderr,
	    "Usage: pkg checksum [-qt <type>] [-c <hash>] <file> ...\n\n");
	fprintf(stderr, "Types: sha256_hex (default), sha256_base32, "
	    "blake2_base32, blake2s_base32\n\n");
	fprintf(stderr, "For more information see 'pkg help checksum'.\n");
}

int
exec_checksum(int argc, char **argv)
{
	const char *type_str = "blake2_base32";
	const char *check = NULL;
	int ch;
	int retcode = EXIT_SUCCESS;

	struct option longopts[] = {
		{ "quiet",	no_argument,		NULL,	'q' },
		{ "type",	required_argument,	NULL,	't' },
		{ "check",	required_argument,	NULL,	'c' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+qt:c:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 't':
			type_str = optarg;
			break;
		case 'c':
			check = optarg;
			break;
		default:
			usage_checksum();
			return (EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage_checksum();
		return (EXIT_FAILURE);
	}

	pkg_checksum_type_t type = pkg_checksum_type_from_string(type_str);
	if (type == PKG_HASH_TYPE_UNKNOWN) {
		warnx("unknown checksum type: %s", type_str);
		return (EXIT_FAILURE);
	}

	while (argc >= 1) {
		if (check != NULL) {
			/* Validate mode */
			int ret = pkg_checksum_validate_file(argv[0], check);
			if (ret != 0) {
				if (!quiet)
					printf("%s: FAILED\n", argv[0]);
				retcode = EXIT_FAILURE;
			} else {
				if (!quiet)
					printf("%s: OK\n", argv[0]);
			}
		} else {
			/* Generate mode */
			char *sum = pkg_checksum_generate_file(argv[0], type);
			if (sum == NULL) {
				warnx("cannot compute checksum for %s",
				    argv[0]);
				retcode = EXIT_FAILURE;
			} else {
				if (quiet)
					printf("%s\n", sum);
				else
					printf("%s (%s)\n", sum, argv[0]);
				free(sum);
			}
		}

		argc--;
		argv++;
	}

	return (retcode);
}
