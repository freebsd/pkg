/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for package file opening.
 * Feed fuzzed tar/xz data to pkg_open_fd().
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char tmp[] = "/tmp/pkg_fuzz.XXXXXX";
	int fd, pfd;
	struct pkg *p = NULL;

	fd = mkstemp(tmp);
	if (fd < 0)
		return (0);

	if (write(fd, data, size) != (ssize_t)size) {
		close(fd);
		(void)unlink(tmp);
		return (0);
	}
	close(fd);

	pfd = open(tmp, O_RDONLY);
	if (pfd < 0) {
		(void)unlink(tmp);
		return (0);
	}

	(void)pkg_open_fd(&p, pfd, 0);

	if (p != NULL)
		pkg_free(p);
	close(pfd);
	(void)unlink(tmp);
	return (0);
}
