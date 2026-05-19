/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for UCL config parsing (ABI options).
 * Write fuzzed data to a temp file, open it as a file descriptor,
 * and pass it to config_parse_abi_options().
 */

#include <err.h>
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
	int fd, conffd;

	fd = mkstemp(tmp);
	if (fd < 0)
		return (0);

	if (write(fd, data, size) != (ssize_t)size) {
		close(fd);
		(void)unlink(tmp);
		return (0);
	}
	close(fd);

	conffd = open(tmp, O_RDONLY);
	if (conffd < 0) {
		(void)unlink(tmp);
		return (0);
	}

	config_parse_abi_options(conffd);

	close(conffd);
	(void)unlink(tmp);

	return (0);
}
