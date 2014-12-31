/*-
 * Copyright (c) 2014 Landon Fuller <landon@landonf.org>
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

#include <bsd_compat.h>
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>

#if !HAVE_UNLINKAT || !HAVE_FSTATAT

static pthread_mutex_t file_at_lock = PTHREAD_MUTEX_INITIALIZER;
static int file_at_dfd = -1;

/**
 * Acquire the cwd mutex and perform fchdir(dfd).
 *
 * On error, the mutex will be released automatically
 * and a non-zero value will be returned.
 *
 * @param dfd The directory file descriptor to be passed to fchdir(), or
 * AT_FDCWD to use the current working directory.
 * @return The fchdir() result.
 */
static int
file_chdir_lock(int dfd)
{
	int ret;

	pthread_mutex_lock(&file_at_lock);

	assert(file_at_dfd == -1);
	file_at_dfd = dfd;

	if (dfd == AT_FDCWD)
		return 0;

	ret = fchdir(dfd);
	if (ret != 0) {
		pthread_mutex_unlock(&file_at_lock);
		return ret;
	} else {
		return ret;
	}
}

/**
 * Release the cwd mutex.
 */
static void
file_chdir_unlock(int dfd)
{
	assert(file_at_dfd == dfd);
	file_at_dfd = -1;

	if (dfd == AT_FDCWD)
		return;

	pthread_mutex_unlock(&file_at_lock);
}
#endif

#if !HAVE_FACCESSAT
int
faccessat(int fd, const char *path, int mode, int flag)
{
	int ret;

	if ((ret = file_chdir_lock(fd) != 0))
		return ret;

	if (flag & AT_EACCESS) {
		ret = eaccess(path, mode);
	} else {
		ret = access(path, mode);
	}

	file_chdir_unlock(fd);
	return ret;
}
#endif

#if !HAVE_READLINKAT
ssize_t
readlinkat(int fd, const char *restrict path, char *restrict buf,
	   size_t bufsize)
{
	int ret;

	if ((ret = file_chdir_lock(fd) != 0))
		return ret;

	ret = readlink(path, buf, bufsize);

	file_chdir_unlock(fd);
	return ret;
}
#endif

#if !HAVE_FSTATAT
int
fstatat(int fd, const char *path, struct stat *buf, int flag)
{
	int ret;

	if ((ret = file_chdir_lock(fd) != 0))
		return ret;

	if (flag & AT_SYMLINK_NOFOLLOW) {
		ret = lstat(path, buf);
	} else {
		ret = stat(path, buf);
	}

	file_chdir_unlock(fd);
	return ret;
}
#endif

#if !HAVE_OPENAT
int
openat(int fd, const char *path, int flags, ...)
{
	int ret;
	va_list ap;

	if ((ret = file_chdir_lock(fd) != 0))
		return ret;

	if (flags & O_CREAT) {
		va_start(ap, flags);
		ret = open(path, flags, va_arg(ap, int));
		va_end(ap);
	} else {
		ret = open(path, flags);
	}

	file_chdir_unlock(fd);
	return ret;
}
#endif

#if !HAVE_UNLINKAT
int
unlinkat(int fd, const char *path, int flag)
{
	int ret;

	if ((ret = file_chdir_lock(fd) != 0))
		return ret;

	if (flag & AT_REMOVEDIR) {
		ret = rmdir(path);
	} else {
		ret = unlink(path);
	}

	file_chdir_unlock(fd);
	return ret;
}
#endif
