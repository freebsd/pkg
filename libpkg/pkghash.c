/*-
 * Copyright (c) 2021 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pkghash.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mum.h>
#include <xmalloc.h>

struct pkghash {
	pkghash_entry *entries;
	size_t capacity;
	size_t count;
	size_t index;
};

pkghash *
pkghash_new(void)
{
	pkghash *table = xmalloc(sizeof(pkghash));
	table->count = 0;
	table->capacity = 128;

	table->entries = xcalloc(table->capacity, sizeof(pkghash_entry));
	return (table);
}

void
pkghash_destroy(pkghash *table)
{
	if (table == NULL)
		return;

	for (size_t i = 0; i < table->capacity; i++) {
		if (table->entries[i].key != NULL)
			free(table->entries[i].key);
		if (table->entries[i].free_func != NULL)
			table->entries[i].free_func(table->entries[i].value);
	}
	free(table->entries);
	free(table);
}

pkghash_entry *
pkghash_get(pkghash *table, const char *key)
{
	if (table == NULL)
		return (NULL);
	uint64_t hash = mum_hash(key, strlen(key), 0);
	size_t index = (size_t)(hash & (uint64_t)(table->capacity -1));

	while (table->entries[index].key != NULL) {
		if (strcmp(key, table->entries[index].key) == 0)
			return (&table->entries[index]);
		index++;
		if (index >= table->capacity)
			index = 0;
	}
	return (NULL);
}

void *
pkghash_get_value(pkghash *table, const char *key)
{
	pkghash_entry *e;

	e = pkghash_get(table, key);
	return (e != NULL ? e->value : NULL);

}

static bool
pkghash_set_entry(pkghash_entry *entries, size_t capacity,
    const char *key, void *value, size_t *pcount, void (*free_func)(void *)) {
	uint64_t hash = mum_hash(key, strlen(key), 0);
	size_t index = (size_t)(hash & (uint64_t)(capacity - 1));

	while (entries[index].key != NULL) {
		if (strcmp(key, entries[index].key) == 0)
			return (false);
		index++;
		if (index >= capacity)
			index = 0;
	}

	if (pcount != NULL) {
		key = xstrdup(key);
		(*pcount)++;
	}
	entries[index].key = (char *)key;
	entries[index].value = value;
	entries[index].free_func = free_func;
	return (true);
}

static bool
pkghash_expand(pkghash *table)
{
	size_t new_capacity = table->capacity * 2;
	if (new_capacity < table->capacity)
		return (false);
	pkghash_entry *new_entries = xcalloc(new_capacity, sizeof(pkghash_entry));

	for (size_t i = 0; i < table->capacity; i++) {
		pkghash_entry entry = table->entries[i];
		if (entry.key != NULL)
			pkghash_set_entry(new_entries, new_capacity, entry.key,
			    entry.value, NULL, entry.free_func);
	}

	free(table->entries);
	table->entries = new_entries;
	table->capacity = new_capacity;
	return (true);
}

bool
pkghash_add(pkghash *table, const char *key, void *value, void (*free_func)(void *))
{
	if (table->count * 2  >= table->capacity && !pkghash_expand(table))
		return (NULL);

	return (pkghash_set_entry(table->entries, table->capacity, key, value,
	    &table->count, free_func));
}

size_t
pkghash_count(pkghash *table)
{
	if (table == 0)
		return (0);
	return (table->count);
}

pkghash_it
pkghash_iterator(pkghash *table)
{
	pkghash_it it;
	it._table = table;
	it._index = 0;
	return (it);
}

bool
pkghash_next(pkghash_it *it)
{
	pkghash *table = it->_table;
	if (table == NULL)
		return (false);
	if (table->count == 0)
		return (false);
	while (it->_index < table->capacity) {
		size_t i = it->_index;
		it->_index++;
		if (table->entries[i].key != NULL) {
			pkghash_entry entry = table->entries[i];
			it->key = entry.key;
			it->value = entry.value;
			return (true);
		}
	}
	return (false);
}

bool
pkghash_del(pkghash *h, const char *key)
{
	pkghash_entry *e = pkghash_get(h, key);
	if (e == NULL)
		return (false);
	free(e->key);
	e->key = NULL;
	if (e->free_func != NULL)
		e->free_func(e->value);
	h->count--;
	return (true);
}

void *
pkghash_delete(pkghash *h, const char *key)
{
	pkghash_entry *e = pkghash_get(h, key);
	if (e == NULL)
		return (NULL);
	free(e->key);
	e->key = NULL;
	h->count--;
	return (e->value);
}
