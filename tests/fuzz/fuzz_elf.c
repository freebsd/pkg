/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for ELF file analysis.
 * Feed fuzzed ELF-like data to pkg_analyse_elf().
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/binfmt.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char tmp[] = "/tmp/pkg_fuzz.XXXXXX";
	int fd;
	struct pkg *p = NULL;
	char *provided = NULL;
	enum pkg_shlib_flags provided_flags = PKG_SHLIB_FLAGS_NONE;

	fd = mkstemp(tmp);
	if (fd < 0)
		return (0);

	if (write(fd, data, size) != (ssize_t)size) {
		close(fd);
		(void)unlink(tmp);
		return (0);
	}
	close(fd);

	if (pkg_new(&p, PKG_FILE) != EPKG_OK) {
		(void)unlink(tmp);
		return (0);
	}

	pkg_analyse_elf(false, p, tmp, &provided, &provided_flags);

	free(provided);
	pkg_free(p);
	(void)unlink(tmp);
	return (0);
}
