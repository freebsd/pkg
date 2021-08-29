#ifndef _PKGHASH_H
#define _PKGHASH_H

#include <stdbool.h>
#include <stddef.h>

typedef struct pkghash pkghash;

pkghash *pkghash_new(void);
void pkghash_destroy(pkghash *table);
bool pkghash_add(pkghash *table, const char *key, void *value, void (*free_func)(void *));
size_t pkghash_count(pkghash *table);

typedef struct {
	char* key;
	void* value;
	pkghash *_table;
	size_t _index;
} pkghash_it;

typedef struct {
	char *key;
	void *value;
	void (*free_func)(void*);
} pkghash_entry;

pkghash_entry *pkghash_get(pkghash *table, const char *key);
pkghash_it pkghash_iterator(pkghash *table);
bool pkghash_next(pkghash_it *it);
#define pkghash_safe_add(_t, _k, _v, _free_func) do { \
	if (_t == NULL)                               \
		_t = pkghash_new();                   \
	else if (pkghash_get(_t, _k) != NULL)         \
		break;                                \
	pkghash_add(_t, _k, _v, _free_func);          \
} while (0);

void pkghash_loopinit(pkghash *h);
pkghash_entry *pkghash_inext(pkghash *h);
bool pkghash_del(pkghash *h, const char *key);
void *pkghash_delete(pkghash *h, const char *key);
void *pkghash_get_value(pkghash *h, const char *key);

#endif

