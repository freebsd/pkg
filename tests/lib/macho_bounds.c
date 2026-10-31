/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#include <sys/types.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>
#include <pkg.h>
#include <private/binfmt_macho.h>

/*
 * The Mach-O load command parsers read the offset of a string from the file.
 * It has to be validated against the size of the command before being used
 * in length arithmetic, otherwise it wraps and the read overflows.
 */

static int
fd_from(const unsigned char *buf, size_t len)
{
	char path[] = "/tmp/pkg-macho.XXXXXX";
	int fd;

	fd = mkstemp(path);
	ATF_REQUIRE(fd != -1);
	unlink(path);
	ATF_REQUIRE_EQ((ssize_t)len, write(fd, buf, len));
	ATF_REQUIRE_EQ(0, lseek(fd, 0, SEEK_SET));

	return (fd);
}

ATF_TC_WITHOUT_HEAD(path_offset);
ATF_TC_BODY(path_offset, tc)
{
	unsigned char cmd[16];
	char *dest = NULL;
	int fd;

	/* an offset one past the command used to wrap the read length */
	memset(cmd, 0, sizeof(cmd));
	cmd[3] = 17;
	fd = fd_from(cmd, sizeof(cmd));
	ATF_REQUIRE_EQ(-1, read_path(fd, false, 16, &dest));
	ATF_REQUIRE_EQ(EINVAL, errno);
	close(fd);

	/* an offset inside the already consumed header */
	memset(cmd, 0, sizeof(cmd));
	fd = fd_from(cmd, sizeof(cmd));
	ATF_REQUIRE_EQ(-1, read_path(fd, false, 16, &dest));
	ATF_REQUIRE_EQ(EINVAL, errno);
	close(fd);

	/* a valid offset reads up to the end of the command */
	memset(cmd, 0, sizeof(cmd));
	cmd[3] = 12;
	memcpy(cmd + 4, "abc", 3);
	fd = fd_from(cmd, sizeof(cmd));
	ATF_REQUIRE_EQ(8, read_path(fd, false, 16, &dest));
	ATF_REQUIRE(dest != NULL);
	ATF_REQUIRE_STREQ("abc", dest);
	free(dest);
	close(fd);
}

ATF_TC_WITHOUT_HEAD(dylib_offset);
ATF_TC_BODY(dylib_offset, tc)
{
	unsigned char cmd[32];
	dylib_t *dest = NULL;
	int fd;

	/* an offset one past the command used to wrap the read length */
	memset(cmd, 0, sizeof(cmd));
	cmd[3] = 25;
	fd = fd_from(cmd, sizeof(cmd));
	ATF_REQUIRE_EQ(-1, read_dylib(fd, false, 24, &dest));
	ATF_REQUIRE_EQ(EINVAL, errno);
	close(fd);

	/* an offset inside the already consumed header */
	memset(cmd, 0, sizeof(cmd));
	fd = fd_from(cmd, sizeof(cmd));
	ATF_REQUIRE_EQ(-1, read_dylib(fd, false, 24, &dest));
	ATF_REQUIRE_EQ(EINVAL, errno);
	close(fd);

	/* a valid offset reads up to the end of the command */
	memset(cmd, 0, sizeof(cmd));
	cmd[3] = 24;
	memcpy(cmd + 16, "abc", 3);
	fd = fd_from(cmd, sizeof(cmd));
	ATF_REQUIRE_EQ(20, read_dylib(fd, false, 28, &dest));
	ATF_REQUIRE(dest != NULL);
	ATF_REQUIRE_STREQ("abc", dest->path);
	free(dest);
	close(fd);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, path_offset);
	ATF_TP_ADD_TC(tp, dylib_offset);

	return (atf_no_error());
}
