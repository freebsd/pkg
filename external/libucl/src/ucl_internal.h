/* Copyright (c) 2013, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef UCL_INTERNAL_H_
#define UCL_INTERNAL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
/* Help embedded builds */
#define HAVE_SYS_TYPES_H
#define HAVE_SYS_MMAN_H
#define HAVE_SYS_STAT_H
#define HAVE_SYS_PARAM_H
#define HAVE_LIMITS_H
#define HAVE_FCNTL_H
#define HAVE_ERRNO_H
#define HAVE_UNISTD_H
#define HAVE_CTYPE_H
#define HAVE_STDIO_H
#define HAVE_STRING_H
#define HAVE_FLOAT_H
#define HAVE_LIBGEN_H
#define HAVE_MATH_H
#define HAVE_STDBOOL_H
#define HAVE_STDINT_H
#define HAVE_STDARG_H
#ifndef _WIN32
# define HAVE_REGEX_H
#endif
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_MMAN_H
# ifndef _WIN32
#  include <sys/mman.h>
# endif
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "utlist.h"
#include "utstring.h"
#include "uthash.h"
#include "ucl.h"
#include "ucl_hash.h"
#include "xxhash.h"

#ifdef HAVE_OPENSSL
#include <openssl/evp.h>
#endif

#ifndef __DECONST
#define __DECONST(type, var)    ((type)(uintptr_t)(const void *)(var))
#endif

/**
 * @file rcl_internal.h
 * Internal structures and functions of UCL library
 */

#define UCL_MAX_RECURSION 16
#define UCL_TRASH_KEY 0
#define UCL_TRASH_VALUE 1

enum ucl_parser_state {
	UCL_STATE_INIT = 0,
	UCL_STATE_OBJECT,
	UCL_STATE_ARRAY,
	UCL_STATE_KEY,
	UCL_STATE_VALUE,
	UCL_STATE_AFTER_VALUE,
	UCL_STATE_ARRAY_VALUE,
	UCL_STATE_SCOMMENT,
	UCL_STATE_MCOMMENT,
	UCL_STATE_MACRO_NAME,
	UCL_STATE_MACRO,
	UCL_STATE_ERROR
};

enum ucl_character_type {
	UCL_CHARACTER_DENIED = 0,
	UCL_CHARACTER_KEY = 1,
	UCL_CHARACTER_KEY_START = 1 << 1,
	UCL_CHARACTER_WHITESPACE = 1 << 2,
	UCL_CHARACTER_WHITESPACE_UNSAFE = 1 << 3,
	UCL_CHARACTER_VALUE_END = 1 << 4,
	UCL_CHARACTER_VALUE_STR = 1 << 5,
	UCL_CHARACTER_VALUE_DIGIT = 1 << 6,
	UCL_CHARACTER_VALUE_DIGIT_START = 1 << 7,
	UCL_CHARACTER_ESCAPE = 1 << 8,
	UCL_CHARACTER_KEY_SEP = 1 << 9,
	UCL_CHARACTER_JSON_UNSAFE = 1 << 10,
	UCL_CHARACTER_UCL_UNSAFE = 1 << 11
};

struct ucl_macro {
	char *name;
	ucl_macro_handler handler;
	void* ud;
	UT_hash_handle hh;
};

struct ucl_stack {
	ucl_object_t *obj;
	struct ucl_stack *next;
	int level;
};

struct ucl_chunk {
	const unsigned char *begin;
	const unsigned char *end;
	const unsigned char *pos;
	size_t remain;
	unsigned int line;
	unsigned int column;
	struct ucl_chunk *next;
};

#ifdef HAVE_OPENSSL
struct ucl_pubkey {
	EVP_PKEY *key;
	struct ucl_pubkey *next;
};
#else
struct ucl_pubkey {
	struct ucl_pubkey *next;
};
#endif

struct ucl_variable {
	char *var;
	char *value;
	size_t var_len;
	size_t value_len;
	struct ucl_variable *next;
};

struct ucl_parser {
	enum ucl_parser_state state;
	enum ucl_parser_state prev_state;
	unsigned int recursion;
	int flags;
	ucl_object_t *top_obj;
	ucl_object_t *cur_obj;
	struct ucl_macro *macroes;
	struct ucl_stack *stack;
	struct ucl_chunk *chunks;
	struct ucl_pubkey *keys;
	struct ucl_variable *variables;
	UT_string *err;
};

/**
 * Unescape json string inplace
 * @param str
 */
size_t ucl_unescape_json_string (char *str, size_t len);

/**
 * Handle include macro
 * @param data include data
 * @param len length of data
 * @param ud user data
 * @param err error ptr
 * @return
 */
bool ucl_include_handler (const unsigned char *data, size_t len, void* ud);

bool ucl_try_include_handler (const unsigned char *data, size_t len, void* ud);

/**
 * Handle includes macro
 * @param data include data
 * @param len length of data
 * @param ud user data
 * @param err error ptr
 * @return
 */
bool ucl_includes_handler (const unsigned char *data, size_t len, void* ud);

size_t ucl_strlcpy (char *dst, const char *src, size_t siz);
size_t ucl_strlcpy_unsafe (char *dst, const char *src, size_t siz);
size_t ucl_strlcpy_tolower (char *dst, const char *src, size_t siz);


#ifdef __GNUC__
static inline void
ucl_create_err (UT_string **err, const char *fmt, ...)
__attribute__ (( format( printf, 2, 3) ));
#endif

static inline void
ucl_create_err (UT_string **err, const char *fmt, ...)

{
	if (*err == NULL) {
		utstring_new (*err);
		va_list ap;
		va_start (ap, fmt);
		utstring_printf_va (*err, fmt, ap);
		va_end (ap);
	}
}

/**
 * Check whether a given string contains a boolean value
 * @param obj object to set
 * @param start start of a string
 * @param len length of a string
 * @return true if a string is a boolean value
 */
static inline bool
ucl_maybe_parse_boolean (ucl_object_t *obj, const unsigned char *start, size_t len)
{
	const unsigned char *p = start;
	bool ret = false, val = false;

	if (len == 5) {
		if ((p[0] == 'f' || p[0] == 'F') && strncasecmp (p, "false", 5) == 0) {
			ret = true;
			val = false;
		}
	}
	else if (len == 4) {
		if ((p[0] == 't' || p[0] == 'T') && strncasecmp (p, "true", 4) == 0) {
			ret = true;
			val = true;
		}
	}
	else if (len == 3) {
		if ((p[0] == 'y' || p[0] == 'Y') && strncasecmp (p, "yes", 3) == 0) {
			ret = true;
			val = true;
		}
		else if ((p[0] == 'o' || p[0] == 'O') && strncasecmp (p, "off", 3) == 0) {
			ret = true;
			val = false;
		}
	}
	else if (len == 2) {
		if ((p[0] == 'n' || p[0] == 'N') && strncasecmp (p, "no", 2) == 0) {
			ret = true;
			val = false;
		}
		else if ((p[0] == 'o' || p[0] == 'O') && strncasecmp (p, "on", 2) == 0) {
			ret = true;
			val = true;
		}
	}

	if (ret) {
		obj->type = UCL_BOOLEAN;
		obj->value.iv = val;
	}

	return ret;
}

/**
 * Check numeric string
 * @param obj object to set if a string is numeric
 * @param start start of string
 * @param end end of string
 * @param pos position where parsing has stopped
 * @param allow_double allow parsing of floating point values
 * @return 0 if string is numeric and error code (EINVAL or ERANGE) in case of conversion error
 */
int ucl_maybe_parse_number (ucl_object_t *obj,
		const char *start, const char *end, const char **pos,
		bool allow_double, bool number_bytes, bool allow_time);


static inline const ucl_object_t *
ucl_hash_search_obj (ucl_hash_t* hashlin, ucl_object_t *obj)
{
	return (const ucl_object_t *)ucl_hash_search (hashlin, obj->key, obj->keylen);
}

static inline ucl_hash_t *
ucl_hash_insert_object (ucl_hash_t *hashlin, const ucl_object_t *obj) UCL_WARN_UNUSED_RESULT;

static inline ucl_hash_t *
ucl_hash_insert_object (ucl_hash_t *hashlin, const ucl_object_t *obj)
{
	if (hashlin == NULL) {
		hashlin = ucl_hash_create ();
	}
	ucl_hash_insert (hashlin, obj, obj->key, obj->keylen);

	return hashlin;
}

/**
 * Emit a single object to string
 * @param obj
 * @return
 */
unsigned char * ucl_object_emit_single_json (const ucl_object_t *obj);

#endif /* UCL_INTERNAL_H_ */
