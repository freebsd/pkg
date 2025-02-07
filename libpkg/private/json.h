/*-
 * Copyright (c) 2024-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define JSMN_PARENT_LINKS 1
#define JSMN_HEADER 1
#define JSMN_STRICT 1
#include <jsmn.h>
#include <stdbool.h>
#include <string.h>

jsmntok_t *jsmn_next(jsmntok_t *tok);
#define jsmn_toklen(x) (x->end - x->start)
bool jsmntok_stringeq(jsmntok_t *tok, const char *line, const char *str);
int jsmntok_nextchild(jsmntok_t *tok, int tokcount, int parent, int me);
