/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for OSVF parsing.
 * Write fuzzed data to a temp file, then call pkg_osvf_open().
 */

#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/pkg_osvf.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char tmp[] = "/tmp/pkg_fuzz.XXXXXX";
	int fd;
	ucl_object_t *obj;

	fd = mkstemp(tmp);
	if (fd < 0)
		return (0);

	if (write(fd, data, size) != (ssize_t)size) {
		close(fd);
		(void)unlink(tmp);
		return (0);
	}
	close(fd);

	obj = pkg_osvf_open(tmp);
	if (obj != NULL)
		ucl_object_unref(obj);

	(void)unlink(tmp);

	return (0);
}
