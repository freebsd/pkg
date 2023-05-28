/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>

#include "bsd_compat.h"

#ifndef HAVE_FUNOPEN

typedef struct {
	int (*readfn)(void *, char *, int);
	int (*writefn)(void *, const char *, int);
	off_t (*seekfn)(void *, off_t, int);
	int (*closefn)(void *);
	void *cookie;
} bsd_cookie_funcs_t;

static ssize_t
cookie_read(void *cookie, char *buf, size_t size)
{
	bsd_cookie_funcs_t *funcs = cookie;
	return (ssize_t)funcs->readfn(funcs->cookie, buf, (int)size);
}

static ssize_t
cookie_write(void *cookie, const char *buf, size_t size)
{
	bsd_cookie_funcs_t *funcs = cookie;
	return (ssize_t)funcs->writefn(funcs->cookie, buf, (int)size);
}

static int
cookie_seek(void *cookie, off64_t *offset, int whence)
{
	bsd_cookie_funcs_t *funcs = cookie;
	off_t v = funcs->seekfn(funcs->cookie, (off_t)*offset, whence);
	if (v < 0)
		return -1;
	*offset = (off64_t)v;
	return 0;
}

static int
cookie_close(void *cookie)
{
	int ret = 0;
	bsd_cookie_funcs_t *funcs = cookie;
	if (funcs->closefn)
		ret = funcs->closefn(funcs->cookie);
	free(cookie);
	return ret;
}

FILE *
funopen(const void *cookie, int (*readfn)(void *, char *, int),
         int (*writefn)(void *, const char *, int),
         off_t (*seekfn)(void *, off_t, int), int (*closefn)(void *))
{
	bsd_cookie_funcs_t *cf = malloc(sizeof(bsd_cookie_funcs_t));
	cf->readfn  = readfn;
	cf->writefn = writefn;
	cf->seekfn  = seekfn;
	cf->closefn = closefn;
	cf->cookie  = __DECONST(void *, cookie);

	cookie_io_functions_t c;
	if (readfn) c.read = cookie_read;
	if (writefn) c.write = cookie_write;
	if (seekfn) c.seek = cookie_seek;
	c.close = cookie_close;

	return fopencookie(cf, "a+", c);
}

#endif
