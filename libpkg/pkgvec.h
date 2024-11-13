#pragma once

#include <stdbool.h>
#include <stddef.h>

#define pkgvec_t(Type) \
  struct { Type *d; int len, cap; }

#define pkgvec_init(v) \
	memset((v), 0, sizeof(*(v)))

#define pkgvec_free(v) \
	free((v)->d);

#define pkgvec_free_and_free(v, free_func)            \
	do {                                          \
		for (size_t _i; _i < (v)->len ; _i++) { \
			free_func((v)->d[_i]);          \
		}                                     \
		free((v)->d);                           \
	} while(0)

#define pkgvec_first(v) \
	(v)->d[0]

#define pkgvec_last(v) \
	(v)->d[d->len -1]

#define pkgvec_clear \
	(v)->len = 0

#define pkgvec_clear_and_free(v, free_func) \
	do {                                          \
		for (size_t _i; _i < (v)->len ; _i++) { \
			free_func((v)->d[_i]);          \
		}                                     \
		(v)->len = 0                            \
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
