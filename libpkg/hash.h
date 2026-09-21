/*
 * Copyright (c) 2021 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _HASH_H
#define _HASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

/*
 * Allocation error handling for hash table operations.
 * Define HASH_ALLOC_ERROR before including this header to override
 * the default abort() behaviour (e.g., longjmp to a recovery point).
 */
#ifndef HASH_ALLOC_ERROR
#define HASH_ALLOC_ERROR abort()
#endif

typedef struct hash hash_t;

hash_t *hash_new(void);
void hash_destroy(hash_t *table);

/*
 * Add an entry.  Returns true on success, false if the key already exists
 * (the existing value is left untouched; use hash_del() first to replace
 * it) or if the table is NULL.
 *
 * The key is copied and owned by the table.  If free_func is not NULL it
 * is called on the value when the entry is removed or the table destroyed.
 */
bool hash_add(hash_t *table, const char *key, void *value,
    void (*free_func)(void *));
size_t hash_count(hash_t *table);

typedef struct {
	char	*key;
	void	*value;
	hash_t	*_table;
	size_t	 _index;
} hash_it;

typedef struct {
	char	*key;
	void	*value;
	void	(*free_func)(void *);
	bool	 tombstone;	/* Deleted entry; probe chain continues */
} hash_entry;

hash_entry	*hash_get(hash_t *table, const char *key);
void		*hash_get_value(hash_t *table, const char *key);
hash_it		 hash_iterator(hash_t *table);
bool		 hash_next(hash_it *it);

/*
 * Iterate over every entry of the table.  The loop variable is a hash_it
 * declared by the macro, exposing the entry as it.key and it.value:
 *
 *	hash_foreach(h, it)
 *		printf("%s = %p\n", it.key, it.value);
 */
#define hash_foreach(table, it) \
	for (hash_it it = hash_iterator(table); hash_next(&it); )

/*
 * hash_del() removes the entry and frees its value through free_func().
 * hash_delete() removes the entry and returns the value, transferring
 * ownership to the caller (free_func is not called).  Both report failure
 * when the key is absent or the table is NULL.
 */
bool		 hash_del(hash_t *h, const char *key);
void		*hash_delete(hash_t *h, const char *key);

/*
 * Add (_k, _v) only when _k is not already present.  Expands to a statement
 * expression yielding true if the entry was added and false otherwise (key
 * already present, allocation failure, or NULL _t).
 *
 * _t is evaluated several times: it must be a simple lvalue.  When the entry
 * is not added _v is neither stored nor freed, so the caller keeps ownership
 * and must free it if it was allocated, e.g.:
 *
 *	if (!hash_safe_add(h, k, v, free))
 *		free(v);
 */
#define hash_safe_add(_t, _k, _v, _free_func) __extension__ ({	\
	bool _added;						\
	if ((_t) == NULL)					\
		(_t) = hash_new();				\
	_added = hash_add((_t), (_k), (_v), (_free_func));	\
	_added;							\
})

#endif /* !_HASH_H */
