/*-
 * Copyright(c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef VEC_H
#define VEC_H

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

#define vec_t(Type) \
  struct { Type *d; size_t len, cap; }

#define vec_init(v) \
	memset((v), 0, sizeof(*(v)))

#define vec_free(v) \
	do { \
		free((v)->d); \
		(v)->d == NULL; \
		memset((v), 0, sizeof(*(v))); \
	} while (0)

#define vec_free_and_free(v, free_func)            \
	do {                                          \
		for (size_t _i=0; _i < (v)->len ; _i++) { \
			free_func((v)->d[_i]);          \
			(v)->d[_i] = NULL;   \
		}                                     \
		vec_free((v)); \
	} while(0)

#define vec_first(v) \
	(v)->d[0]

#define vec_last(v) \
	(v)->d[(v)->len -1]

#define vec_clear(v) \
	(v)->len = 0

#define vec_clear_and_free(v, free_func) \
	do {                                          \
		for (size_t _i=0; _i < (v)->len ; _i++) { \
			free_func((v)->d[_i]);          \
			(v)->d[_i] = NULL;   \
		}                                     \
		(v)->len = 0;                            \
	} while (0)

#define vec_push(v, _d)                                            \
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

#define vec_pop(v) \
	(v)->d[--(v)->len]

typedef vec_t(char *) charv_t;
typedef vec_t(const char *) c_charv_t;
#endif
