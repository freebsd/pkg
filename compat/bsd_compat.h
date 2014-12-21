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

#ifndef _BSD_COMPAT_H
#define _BSD_COMPAT_H

#include <sys/stat.h>

#include "pkg_config.h"
#include "endian_util.h"

char *bsd_dirname(const char *);
char *bsd_basename(const char *);

#if !HAVE_EACCESS
#define eaccess(_p, _m) access(_p, _m)
#endif

#if !HAVE_GR_MAKE
#include "gr_util.h"
#endif

#if !HAVE_HUMANIZE_NUMBER
#include "humanize_number.h"
#endif

#if !HAVE_CLOSEFROM
void closefrom(int lowfd);
#endif

#ifndef AT_FDCWD
#define AT_FDCWD		-100
#endif

#ifndef AT_EACCESS
#define AT_EACCESS		0x100
#endif

#ifndef AT_SYMLINK_NOFOLLOW
#define	AT_SYMLINK_NOFOLLOW	0x200
#endif

#if !HAVE_FACCESSAT
int faccessat(int fd, const char *path, int mode, int flag);
#endif

#if !HAVE_FSTATAT
int fstatat(int fd, const char *path, struct stat *buf, int flag);
#endif

#if !HAVE_OPENAT
int openat(int fd, const char *path, int flags, ...);
#endif

#if !HAVE_READLINKAT
ssize_t readlinkat(int fd, const char *restrict path, char *restrict buf, size_t bufsize);
#endif

#if !HAVE_UNLINKAT
#define AT_REMOVEDIR	0x800
int unlinkat(int fd, const char *path, int flag);
#endif

#if !HAVE_STRTONUM
long long strtonum(const char *, long long, long long, const char **);
#endif

#endif
