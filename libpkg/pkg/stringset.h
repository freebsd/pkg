/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright(c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#ifndef _STRINGSET_H
#define _STRINGSET_H

#include <stdbool.h>
#include <stddef.h>

typedef struct stringset stringset_t;

stringset_t *stringset_new(void);
void stringset_destroy(stringset_t *set);

/* Returns true if the key was newly added, false if it already existed. */
bool stringset_add(stringset_t *set, const char *key);

/* Returns true if the key is in the set. */
bool stringset_contains(const stringset_t *set, const char *key);

/* Returns the number of elements. */
size_t stringset_count(const stringset_t *set);

/* Remove and return true if the key was present. */
bool stringset_del(stringset_t *set, const char *key);

typedef const char *stringset_it;

typedef struct {
	const stringset_t *_set;
	size_t _index;
} stringset_iter;

stringset_iter stringset_iterator(const stringset_t *set);
stringset_it stringset_next(stringset_iter *it);

#define stringset_foreach(set, it) \
	for (stringset_iter _ss_iter = stringset_iterator(set); \
	     (it = stringset_next(&_ss_iter)) != NULL; )

#define stringset_safe_add(_s, _k) do { \
	if (*(_s) == NULL) \
		*(_s) = stringset_new(); \
	stringset_add(*(_s), (_k)); \
} while (0)

#endif
