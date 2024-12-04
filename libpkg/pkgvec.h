/*-
 * Copyright(c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

#define pkgvec_t(Type) \
  struct { Type *d; size_t len, cap; }

#define pkgvec_init(v) \
	memset((v), 0, sizeof(*(v)))

#define pkgvec_free(v) \
	do { \
		free((v)->d); \
		(v)->d == NULL; \
		memset((v), 0, sizeof(*(v))); \
	} while (0)

#define pkgvec_free_and_free(v, free_func)            \
	do {                                          \
		for (size_t _i=0; _i < (v)->len ; _i++) { \
			free_func((v)->d[_i]);          \
			(v)->d[_i] = NULL;   \
		}                                     \
		pkgvec_free((v)); \
	} while(0)

#define pkgvec_first(v) \
	(v)->d[0]

#define pkgvec_last(v) \
	(v)->d[(v)->len -1]

#define pkgvec_clear(v) \
	(v)->len = 0

#define pkgvec_clear_and_free(v, free_func) \
	do {                                          \
		for (size_t _i=0; _i < (v)->len ; _i++) { \
			free_func((v)->d[_i]);          \
			(v)->d[_i] = NULL;   \
		}                                     \
		(v)->len = 0;                            \
	} while (0)

#define pkgvec_push(v, _d)                                            \
	do {                                                          \
		if ((v)->len + 1 > (v)->cap) {                            \
			if ((v)->cap == 0)                              \
				(v)->cap = 1;                          \
			else                                          \
				(v)->cap *=2;                           \
			(v)->d = realloc((v)->d, (v)->cap * sizeof(*(v)->d)); \
			if ((v)->d == NULL)                             \
				abort();                              \
		}                                                     \
		(v)->d[(v)->len++] = (_d);                                  \
	} while (0)                                                   \

#define pkgvec_pop(v) \
	(v)->d[--(v)->len]

typedef pkgvec_t(char *) charv_t;
typedef pkgvec_t(const char *) c_charv_t;
