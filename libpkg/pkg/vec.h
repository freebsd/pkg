/*-
 * Copyright(c) 2024-2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef VEC_H
#define VEC_H

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define vec_t(Type) \
  struct { Type *d; size_t len, cap; }

#define vec_init() \
	{ .d = NULL, .len = 0, .cap = 0 }

#define vec_foreach(list, __i) \
	for (size_t __i = 0; __i < (list).len; __i++)

/* ssize_t because the value can be negative */
#define vec_rforeach(list, __i) \
	for (ssize_t __i = ((ssize_t)(list).len) -1 ; __i >= 0; __i--)

#define vec_free(v) \
	do { \
		free((v)->d); \
		memset((v), 0, sizeof(*(v))); \
	} while (0)

#define vec_free_and_free(v, free_func)        \
	do {                                   \
		vec_foreach(*(v), __vec_i) {     \
			free_func((v)->d[__vec_i]); \
			(v)->d[__vec_i] = NULL;     \
		}                              \
		vec_free((v));                 \
	} while(0)

/*
 * First (vec_first) and last (vec_last) element.  Like vec_pop() they are
 * statement expressions, so not lvalues, and yield a zeroed element when the
 * vector is empty.
 */
#define vec_first(v) __extension__ \
	({ \
		__typeof__(*(v)->d) _ret = { 0 }; \
		if ((v)->len > 0) \
			_ret = (v)->d[0]; \
		_ret; \
	})

#define vec_last(v) __extension__ \
	({ \
		__typeof__(*(v)->d) _ret = { 0 }; \
		if ((v)->len > 0) \
			_ret = (v)->d[(v)->len - 1]; \
		_ret; \
	})

#define vec_clear(v) \
	(v)->len = 0

#define vec_clear_and_free(v, free_func)       \
	do {                                   \
		vec_foreach(*(v), __vec_i) {     \
			free_func((v)->d[__vec_i]); \
			(v)->d[__vec_i] = NULL;     \
		}                              \
		(v)->len = 0;                  \
	} while (0)

#define vec_push(v, _d)                                            \
	do {                                                          \
		if ((v)->len >= (v)->cap) {                            \
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

#define vec_push_front(v, _d)                                      \
	do {                                                       \
		if ((v)->len >= (v)->cap) {                         \
			if ((v)->cap == 0)                          \
				(v)->cap = 1;                      \
			else                                       \
				(v)->cap *= 2;                     \
			(v)->d = realloc((v)->d, (v)->cap * sizeof(*(v)->d)); \
			if ((v)->d == NULL)                         \
				abort();                           \
		}                                                  \
		for (size_t _i = (v)->len; _i > 0; _i--)           \
			(v)->d[_i] = (v)->d[_i - 1];              \
		(v)->d[0] = (_d);                                  \
		(v)->len++;                                        \
	} while (0)

/*
 * Pop the last (vec_pop) or first (vec_pop_front) element.  Both expand to a
 * statement expression, so they yield a value and are not lvalues.  On an
 * empty vector they return a zeroed element and leave the vector unchanged.
 */
#define vec_pop(v) __extension__                               \
	({                                                     \
		__typeof__(*(v)->d) _ret = { 0 };              \
		if ((v)->len > 0)                              \
			_ret = (v)->d[--(v)->len];             \
		_ret;                                          \
	})

#define vec_pop_front(v) __extension__                             \
	({                                                         \
		__typeof__(*(v)->d) _ret = { 0 };                  \
		if ((v)->len > 0) {                                \
			_ret = (v)->d[0];                          \
			vec_remove(v, 0);                          \
		}                                                  \
		_ret;                                              \
	})

#define vec_remove(v, cnt) \
	do {                                                    \
		if ((v)->len > 0 && (cnt) < (v)->len) {         \
			for (size_t _i = (cnt);                 \
			    _i < (v)->len - 1; _i++) {          \
				(v)->d[_i] = (v)->d[_i + 1];    \
			}                                       \
			(v)->len--;                             \
		}                                               \
	} while (0)

#define vec_remove_and_free(v, cnt, free_func) \
	do {                                                    \
		if ((v)->len > 0 && (cnt) < (v)->len) {         \
			free_func((v)->d[cnt]);                 \
			vec_remove(v, cnt);                     \
		}                                               \
	} while (0)

/*
 * Remove the element at the given index and replace it with the last
 * element in the vec. Does not preserve order, but is O(1).
 */
#define vec_swap_remove(v, index)                        \
	do {                                             \
		if ((v)->len > 0 && (index) < (v)->len) {\
			if ((index) < (v)->len - 1) {    \
				(v)->d[index] = vec_last(v); \
			}                                \
			(v)->len--;                      \
		}                                        \
	} while (0)

#define vec_len(v) \
	(v)->len

#define DEFINE_VEC_INSERT_SORTED_PROTO(type, name, element_type) \
	element_type *name##_insert_sorted(type *v, element_type el)

#define DEFINE_VEC_INSERT_SORTED_FUNC(type, _name, element_type, compare_func) \
	element_type *_name##_insert_sorted(type *v, element_type el) { \
		/* Verify if the element already exists */ \
		if (v->len > 0) { \
			element_type *found = bsearch(&el, v->d, v->len, sizeof(element_type), compare_func); \
			if (found != NULL){ \
				return (found); \
			} \
		} \
		if (v->len >= v->cap) { \
			v->cap = (v->cap == 0) ? 1 : v->cap * 2; \
			v->d = realloc(v->d, v->cap * sizeof(element_type)); \
			if (v->d == NULL) \
				abort(); \
		} \
		/* Find where to insert */ \
		size_t i; \
		for (i = v->len; i > 0 && compare_func(&v->d[i - 1], &el) > 0; i--) { \
			v->d[i] = v->d[i - 1]; \
		} \
		v->d[i] = el; \
		v->len++; \
		return (NULL); \
	}

typedef vec_t(char *) charv_t;
typedef vec_t(const char *) c_charv_t;

#endif
