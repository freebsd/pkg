/*-
 * Copyright (c) 2024-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define JSMN_PARENT_LINKS 1
#define JSMN_STRICT 1
#include <jsmn.h>
#include <libpkg/private/json.h>
#include <stdio.h>

jsmntok_t *
jsmn_next(jsmntok_t *tok) {
	jsmntok_t *cur = tok;
	int cnt = tok->size;

	while (cnt--) {
		cur++;
		cur = jsmn_next(cur);
	}
	return (cur);
}

bool
jsmntok_stringeq(jsmntok_t *tok, const char *line, const char *str) {
	return (strncmp(str, line + tok->start, jsmn_toklen(tok)) == 0);
}

int
jsmntok_nextchild(jsmntok_t *tok, int tokcount, int parent, int me)
{
	for (int i = me + 1; i < tokcount; i++) {
		if ((tok + i)->parent == parent)
			return (i);
		/* skip all the objet child, useful if this is an array
		 * or an object
		 */
		i += (tok + i)->size;
	}
	return (-1);
}
