/*
 * Copyright (c) 2021 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Open-addressing hash table with automatic growth.
 * Adapted from pkg's pkghash for general use.
 */

#include "hash.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "strhash.h"

#define STREQ(s1, s2)	(strcmp((s1), (s2)) == 0)

/*
 * Allocation helpers using HASH_ALLOC_ERROR (defined in hash.h).
 * Defaults to abort() unless overridden by the includer.
 */
#define HASH_MALLOC(ptr, size) do {			\
	ptr = malloc(size);				\
	if (ptr == NULL)				\
		HASH_ALLOC_ERROR;			\
} while (0)

#define HASH_CALLOC(ptr, n, size) do {			\
	ptr = calloc(n, size);				\
	if (ptr == NULL)				\
		HASH_ALLOC_ERROR;			\
} while (0)

struct hash {
	hash_entry	*entries;
	size_t		 capacity;
	size_t		 count;
	size_t		 tombstones;
};

hash_t *
hash_new(void)
{
	hash_t *table;

	HASH_MALLOC(table, sizeof(*table));
	table->count = 0;
	table->tombstones = 0;
	table->capacity = 128;
	HASH_CALLOC(table->entries, table->capacity, sizeof(hash_entry));
	return (table);
}

void
hash_destroy(hash_t *table)
{
	size_t i;

	if (table == NULL)
		return;

	for (i = 0; i < table->capacity; i++) {
		if (table->entries[i].key != NULL)
			free(table->entries[i].key);
		if (table->entries[i].free_func != NULL)
			table->entries[i].free_func(table->entries[i].value);
	}
	free(table->entries);
	free(table);
}

hash_entry *
hash_get(hash_t *table, const char *key)
{
	uint64_t h;
	size_t index;

	if (table == NULL || key == NULL)
		return (NULL);
	h = strhash(key, strlen(key));
	index = (size_t)(h & (uint64_t)(table->capacity - 1));

	while (table->entries[index].key != NULL ||
	    table->entries[index].tombstone) {
		if (table->entries[index].key != NULL &&
		    STREQ(key, table->entries[index].key))
			return (&table->entries[index]);
		index++;
		if (index >= table->capacity)
			index = 0;
	}
	return (NULL);
}

void *
hash_get_value(hash_t *table, const char *key)
{
	hash_entry *e;

	e = hash_get(table, key);
	return (e != NULL ? e->value : NULL);
}

static bool
hash_set_entry(hash_entry *entries, size_t capacity,
    const char *key, void *value, size_t *pcount,
    void (*free_func)(void *))
{
	uint64_t h;
	size_t index;

	h = strhash(key, strlen(key));
	index = (size_t)(h & (uint64_t)(capacity - 1));

	while (entries[index].key != NULL ||
	    entries[index].tombstone) {
		if (entries[index].key != NULL &&
		    STREQ(key, entries[index].key))
			return (false);
		index++;
		if (index >= capacity)
			index = 0;
	}

	if (pcount != NULL) {
		char *kdup = strdup(key);

		if (kdup == NULL)
			HASH_ALLOC_ERROR;
		key = kdup;
		(*pcount)++;
	}
	entries[index].key = (char *)key;
	entries[index].value = value;
	entries[index].free_func = free_func;
	entries[index].tombstone = false;
	return (true);
}

static bool
hash_expand(hash_t *table)
{
	size_t new_capacity;
	hash_entry *new_entries;
	size_t i;

	new_capacity = table->capacity * 2;
	if (new_capacity < table->capacity)
		return (false);
	HASH_CALLOC(new_entries, new_capacity, sizeof(hash_entry));

	for (i = 0; i < table->capacity; i++) {
		hash_entry entry = table->entries[i];
		if (entry.key != NULL)
			hash_set_entry(new_entries, new_capacity,
			    entry.key, entry.value, NULL,
			    entry.free_func);
	}

	free(table->entries);
	table->entries = new_entries;
	table->capacity = new_capacity;
	table->tombstones = 0;
	return (true);
}

bool
hash_add(hash_t *table, const char *key, void *value,
    void (*free_func)(void *))
{

	if (table == NULL || key == NULL)
		return (false);
	if ((table->tombstones > table->capacity / 4 ||
	    table->count * 2 >= table->capacity) &&
	    !hash_expand(table))
		return (false);

	return (hash_set_entry(table->entries, table->capacity,
	    key, value, &table->count, free_func));
}

size_t
hash_count(hash_t *table)
{

	if (table == NULL)
		return (0);
	return (table->count);
}

hash_it
hash_iterator(hash_t *table)
{
	hash_it it = { 0 };

	it._table = table;
	return (it);
}

bool
hash_next(hash_it *it)
{
	hash_t *table;

	table = it->_table;
	if (table == NULL || table->count == 0)
		return (false);
	while (it->_index < table->capacity) {
		size_t i = it->_index;

		it->_index++;
		if (table->entries[i].key != NULL) {
			it->key = table->entries[i].key;
			it->value = table->entries[i].value;
			return (true);
		}
	}
	return (false);
}

bool
hash_del(hash_t *table, const char *key)
{
	hash_entry *e;

	e = hash_get(table, key);
	if (e == NULL)
		return (false);
	free(e->key);
	e->key = NULL;
	if (e->free_func != NULL)
		e->free_func(e->value);
	/* Clear the freed value so hash_destroy() does not free it again. */
	e->value = NULL;
	e->free_func = NULL;
	e->tombstone = true;
	table->tombstones++;
	table->count--;
	return (true);
}

void *
hash_delete(hash_t *table, const char *key)
{
	hash_entry *e;
	void *value;

	e = hash_get(table, key);
	if (e == NULL)
		return (NULL);
	free(e->key);
	e->key = NULL;
	value = e->value;
	/* Ownership is transferred to the caller: drop the free_func so
	 * hash_destroy() does not free the value again. */
	e->value = NULL;
	e->free_func = NULL;
	e->tombstone = true;
	table->tombstones++;
	table->count--;
	return (value);
}
