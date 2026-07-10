/*-
 * Copyright(c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mum.h"
#include "xmalloc.h"

#include "pkg/stringset.h"

/*
 * Open-addressing hash set storing only strings (no values).
 * Each entry is either:
 *   - key != NULL  → occupied
 *   - key == NULL  → empty or tombstone
 *
 * We track tombstones separately to know when to rehash.
 *
 * Entry size: 8 bytes (key pointer) vs pkghash_entry's 32 bytes
 * (key + value + free_func + tombstone bool + padding).
 *
 * That's a 4x memory saving per entry.
 */

struct stringset_entry {
	char *key;
	bool tombstone;
};

struct stringset {
	struct stringset_entry *entries;
	size_t capacity;
	size_t count;
	size_t tombstones;
};

stringset_t *
stringset_new(void)
{
	stringset_t *set = xmalloc(sizeof(stringset_t));
	set->count = 0;
	set->tombstones = 0;
	set->capacity = 128;
	set->entries = xcalloc(set->capacity, sizeof(struct stringset_entry));
	return (set);
}

void
stringset_destroy(stringset_t *set)
{
	if (set == NULL)
		return;

	for (size_t i = 0; i < set->capacity; i++) {
		if (set->entries[i].key != NULL)
			free(set->entries[i].key);
	}
	free(set->entries);
	free(set);
}

static const struct stringset_entry *
stringset_find(const stringset_t *set, const char *key)
{
	if (set == NULL)
		return (NULL);

	uint64_t hash = mum_hash(key, strlen(key), 0);
	size_t index = (size_t)(hash & (uint64_t)(set->capacity - 1));

	for (size_t i = 0; i < set->capacity; i++) {
		if (set->entries[index].key == NULL &&
		    !set->entries[index].tombstone)
			return (NULL);
		if (set->entries[index].key != NULL &&
		    strcmp(key, set->entries[index].key) == 0)
			return (&set->entries[index]);
		index++;
		if (index >= set->capacity)
			index = 0;
	}
	return (NULL);
}

bool
stringset_contains(const stringset_t *set, const char *key)
{
	return (stringset_find(set, key) != NULL);
}

static bool
stringset_insert_entry(struct stringset_entry *entries, size_t capacity,
    const char *key, size_t *pcount)
{
	uint64_t hash = mum_hash(key, strlen(key), 0);
	size_t index = (size_t)(hash & (uint64_t)(capacity - 1));

	for (size_t i = 0; i < capacity; i++) {
		if (entries[index].key == NULL && !entries[index].tombstone) {
			if (pcount != NULL) {
				entries[index].key = xstrdup(key);
				(*pcount)++;
			} else {
				entries[index].key = (char *)key;
			}
			entries[index].tombstone = false;
			return (true);
		}
		if (entries[index].key != NULL &&
		    strcmp(key, entries[index].key) == 0)
			return (false);
		index++;
		if (index >= capacity)
			index = 0;
	}
	return (false);
}

static bool
stringset_expand(stringset_t *set)
{
	size_t newcap = set->capacity * 2;
	if (newcap < set->capacity)
		return (false);

	struct stringset_entry *new_entries =
	    xcalloc(newcap, sizeof(struct stringset_entry));

	for (size_t i = 0; i < set->capacity; i++) {
		if (set->entries[i].key != NULL)
			stringset_insert_entry(new_entries, newcap,
			    set->entries[i].key, NULL);
	}

	free(set->entries);
	set->entries = new_entries;
	set->capacity = newcap;
	set->tombstones = 0;
	return (true);
}

bool
stringset_add(stringset_t *set, const char *key)
{
	if ((set->tombstones > set->capacity / 4 ||
	    set->count * 2 >= set->capacity) &&
	    !stringset_expand(set))
		return (false);

	return (stringset_insert_entry(set->entries, set->capacity,
	    key, &set->count));
}

size_t
stringset_count(const stringset_t *set)
{
	if (set == NULL)
		return (0);
	return (set->count);
}

bool
stringset_del(stringset_t *set, const char *key)
{
	const struct stringset_entry *e = stringset_find(set, key);
	if (e == NULL)
		return (false);

	/* const-cast: we need to modify the entry */
	struct stringset_entry *we = (struct stringset_entry *)e;
	free(we->key);
	we->key = NULL;
	we->tombstone = true;
	set->tombstones++;
	set->count--;
	return (true);
}

stringset_iter
stringset_iterator(const stringset_t *set)
{
	stringset_iter it = { 0 };
	it._set = set;
	return (it);
}

stringset_it
stringset_next(stringset_iter *it)
{
	const stringset_t *set = it->_set;
	if (set == NULL)
		return (NULL);
	if (set->count == 0)
		return (NULL);

	while (it->_index < set->capacity) {
		size_t i = it->_index;
		it->_index++;
		if (set->entries[i].key != NULL)
			return (set->entries[i].key);
	}
	return (NULL);
}
