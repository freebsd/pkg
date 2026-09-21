/*
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef XMALLOC_H
#define XMALLOC_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Allocation helpers with configurable error handling.
 *
 * By default, allocation failures call abort().  To use a custom handler
 * (e.g., longjmp), define XALLOC_ERROR before including this header:
 *
 *   #include <setjmp.h>
 *   extern jmp_buf my_jmp_buf;
 *   #define XALLOC_ERROR longjmp(my_jmp_buf, 1)
 *   #include "xmalloc.h"
 */

#ifndef XALLOC_ERROR
#define XALLOC_ERROR abort()
#endif

static inline void *xmalloc(size_t size)
{
	void *ptr = malloc(size);
	if (ptr == NULL)
		XALLOC_ERROR;
	return (ptr);
}

static inline void *xcalloc(size_t n, size_t size)
{
	void *ptr = calloc(n, size);
	if (ptr == NULL)
		XALLOC_ERROR;
	return (ptr);
}

static inline void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (ptr == NULL)
		XALLOC_ERROR;
	return (ptr);
}

static inline char *xstrdup(const char *str)
{
	char *s = strdup(str);
	if (s == NULL)
		XALLOC_ERROR;
	return (s);
}

static inline char *xstrndup(const char *str, size_t n)
{
	char *s = strndup(str, n);
	if (s == NULL)
		XALLOC_ERROR;
	return (s);
}

static inline int xasprintf(char **ret, const char *fmt, ...)
{
	va_list ap;
	int i;

	va_start(ap, fmt);
	i = vasprintf(ret, fmt, ap);
	va_end(ap);

	if (i < 0 || *ret == NULL)
		XALLOC_ERROR;

	return (i);
}
#endif
