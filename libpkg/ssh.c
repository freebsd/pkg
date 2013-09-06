/*-
 * Copyright (c) 2013 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <inttypes.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"

int
pkg_sshserve(void)
{
	struct stat st;
	char *line = NULL;
	char *file, *age;
	size_t linecap = 0, r;
	ssize_t linelen;
	time_t mtime = 0;
	const char *errstr;
	FILE *f;
	char buf[BUFSIZ];
	char fpath[MAXPATHLEN];
	const char *restricted = NULL;

	pkg_config_string(PKG_CONFIG_SSH_RESTRICT_DIR, &restricted);

	printf("ok: pkg "PKGVERSION"\n");
	for (;;) {
		if ((linelen = getline(&line, &linecap, stdin)) <= 0)
			continue;

		/* trim cr */
		if (line[linelen - 1] == '\n')
			line[linelen - 1] = '\0';

		if (strcmp(line, "quit") == 0)
			return (EPKG_OK);

		if (strncmp(line, "get ", 4) != 0) {
			printf("ko: unknown command '%s'\n", line);
			continue;
		}

		file = line + 4;
		age = file;
		while (!isspace(*age)) {
			if (*age == '\0') {
				age = NULL;
				break;
			}
			age++;
		}

		if (age == NULL) {
			printf("ko: bad command get, expecting 'get file age'\n");
			continue;
		}

		*age = '\0';
		age++;

		while (isspace(*age)) {
			if (*age == '\0') {
				age = NULL;
				break;
			}
			age++;
		}

		if (age == NULL) {
			printf("ko: bad command get, expecting 'get file age'\n");
			continue;
		}

		mtime = strtonum(age, 0, LONG_MAX, &errstr);
		if (errstr) {
			printf("ko: bad number %s: %s\n", age, errstr);
			continue;
		}

		if (restricted != NULL) {
			file = realpath(file, fpath);
			if (strncmp(file, restricted, strlen(restricted)) != 0) {
				printf("ko: file not found\n");
				continue;
			}
		}

		if (stat(file, &st) == -1) {
			printf("ko: file not found\n");
			continue;
		}

		if (!S_ISREG(st.st_mode)) {
			printf("ko: not a file\n");
			continue;
		}

		if (st.st_mtime <= mtime) {
			printf("ok: 0\n");
			continue;
		}

		printf("ok: %" PRIdMAX "\n", (intmax_t)st.st_size);
		f = fopen(file, "r");

		while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
			fwrite(buf, 1, r, stdout);

		fclose(f);
	}

	free(line);

	return (EPKG_OK);
}
