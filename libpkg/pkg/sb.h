/*-
 * Copyright(c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Usage:
 *   sb_t sb = sb_init();
 *   sb_printf(&sb, "hello %s", "world");
 *   sb_cat(&sb, "!");
 *   char *s = sb_get(&sb);  // transfers ownership
 *   free(s);
 *
 *   // stack-allocate and free:
 *   sb_t sb2 = sb_init();
 *   sb_printf(&sb2, "answer = %d", 42);
 *   puts(sb2.d);   // direct access
 *   sb_fini(&sb2); // frees buffer
 */

#ifndef SB_H
#define SB_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define SB_PRINTF_ATTR(f, a) __attribute__((format(printf, f, a)))
#else
#define SB_PRINTF_ATTR(f, a)
#endif

typedef struct {
	char   *d;		/* buffer (always NUL-terminated) */
	size_t  len;		/* current string length (excluding NUL) */
	size_t  cap;		/* allocated capacity */
} sb_t;

#define sb_init() \
	{ .d = NULL, .len = 0, .cap = 0 }

/* Larger functions defined in sb.c (non-inline to save space). */
void sb_grow(sb_t *sb, size_t needed);
void sb_cat(sb_t *sb, const char *s);
void sb_cat_n(sb_t *sb, const char *s, size_t n);
void sb_cat_c(sb_t *sb, char c);
void sb_printf(sb_t *sb, const char *fmt, ...) SB_PRINTF_ATTR(2, 3);
void sb_fini(sb_t *sb);
void sb_reset(sb_t *sb);
char *sb_get(sb_t *sb);
char *sb_str(sb_t *sb);

#endif /* SB_H */
