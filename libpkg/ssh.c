/*-
 * Copyright (c) 2013-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#ifdef HAVE_CAPSICUM
#include <sys/capability.h>
#endif
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <inttypes.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "private/event.h"

int
pkg_sshserve(int fd)
{
	struct stat st;
	char *line = NULL;
	char *file, *age;
	size_t linecap = 0, r;
	ssize_t linelen;
	time_t mtime = 0;
	const char *errstr;
	int ffd;
	char buf[BUFSIZ];
	char fpath[MAXPATHLEN];
	char rpath[MAXPATHLEN];
	const char *restricted = NULL;

	restricted = pkg_object_string(pkg_config_get("SSH_RESTRICT_DIR"));

	printf("ok: pkg "PKGVERSION"\n");
	for (;;) {
		if ((linelen = getline(&line, &linecap, stdin)) < 0)
			break;

		if (linelen == 0)
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

		if (*file == '/')
			file++;
		else if (*file == '\0') {
			printf("ko: bad command get, expecting 'get file age'\n");
			continue;
		}

		pkg_debug(1, "SSH server> file requested: %s", file);

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

#ifdef HAVE_CAPSICUM
		if (!cap_sandboxed() && restricted != NULL) {
#else
		if (restricted != NULL) {
#endif
			chdir(restricted);
			if (realpath(file, fpath) == NULL ||
			    realpath(restricted, rpath) == NULL ||
			    strncmp(fpath, rpath, strlen(rpath)) != 0) {
				printf("ko: file not found\n");
				continue;
			}
		}

		if (fstatat(fd, file, &st, 0) == -1) {
			pkg_debug(1, "SSH server> fstatat failed");
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

		if ((ffd = openat(fd, file, O_RDONLY)) == -1) {
			printf("ko: file not found\n");
			continue;
		}

		printf("ok: %" PRIdMAX "\n", (intmax_t)st.st_size);
		pkg_debug(1, "SSH server> sending ok: %" PRIdMAX "", (intmax_t)st.st_size);

		while ((r = read(ffd, buf, sizeof(buf))) > 0) {
			pkg_debug(1, "SSH server> sending data");
			fwrite(buf, 1, r, stdout);
		}

		pkg_debug(1, "SSH server> finished");

		close(ffd);
	}

	free(line);

	return (EPKG_OK);
}
