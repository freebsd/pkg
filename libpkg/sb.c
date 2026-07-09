/*-
 * Copyright(c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 */

#include "pkg/sb.h"

/* Growth strategy modeled on FreeBSD's sbuf(9): double while the buffer
 * is smaller than one page, then grow linearly by page-sized increments.
 * This keeps the capacity close to the used length for large buffers,
 * unlike plain doubling which over-allocates up to 2x. */
#define SB_MINEXTENDSIZE 16		/* power of 2 */
#define SB_MAXEXTENDSIZE 4096		/* one page */
#define SB_MAXEXTENDINCR 4096		/* page size */

void
sb_grow(sb_t *sb, size_t needed)
{
	size_t newcap;

	if (needed <= sb->cap)
		return;
	if (needed < SB_MAXEXTENDSIZE) {
		newcap = SB_MINEXTENDSIZE;
		while (newcap < needed)
			newcap *= 2;
	} else {
		newcap = (needed + (SB_MAXEXTENDINCR - 1)) &
		    ~(size_t)(SB_MAXEXTENDINCR - 1);
	}
	char *nd = realloc(sb->d, newcap);
	if (nd == NULL)
		abort();
	sb->d = nd;
	sb->cap = newcap;
	sb->d[sb->len] = '\0';
}

void
sb_cat(sb_t *sb, const char *s)
{
	size_t slen = strlen(s);
	sb_grow(sb, sb->len + slen + 1);
	memcpy(sb->d + sb->len, s, slen + 1);
	sb->len += slen;
}

void
sb_cat_n(sb_t *sb, const char *s, size_t n)
{
	if (n == 0)
		return;
	sb_grow(sb, sb->len + n + 1);
	memcpy(sb->d + sb->len, s, n);
	sb->len += n;
	sb->d[sb->len] = '\0';
}

void
sb_cat_c(sb_t *sb, char c)
{
	sb_grow(sb, sb->len + 2);
	sb->d[sb->len++] = c;
	sb->d[sb->len] = '\0';
}

void
sb_printf(sb_t *sb, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (sb->cap == 0)
		sb_grow(sb, 512);

	for (;;) {
		size_t remain = sb->cap - sb->len;
		va_start(ap, fmt);
		n = vsnprintf(sb->d + sb->len, remain, fmt, ap);
		va_end(ap);

		if (n < 0)
			abort();

		if ((size_t)n < remain) {
			sb->len += (size_t)n;
			return;
		}

		sb_grow(sb, sb->len + (size_t)n + 1);
	}
}

void
sb_fini(sb_t *sb)
{
	if (sb == NULL)
		return;
	free(sb->d);
	sb->d = NULL;
	sb->len = 0;
	sb->cap = 0;
}

void
sb_reset(sb_t *sb)
{
	if (sb == NULL)
		return;
	sb->len = 0;
	if (sb->d)
		sb->d[0] = '\0';
}

char *
sb_get(sb_t *sb)
{
	if (sb == NULL)
		return (NULL);
	if (sb->d == NULL) {
		sb_grow(sb, 1);
		sb->d[0] = '\0';
	}
	char *ret = sb->d;
	sb->d = NULL;
	sb->len = 0;
	sb->cap = 0;
	return (ret);
}

char *
sb_str(sb_t *sb)
{
	if (sb->d == NULL) {
		sb_grow(sb, 1);
		sb->d[0] = '\0';
	}
	return (sb->d);
}

