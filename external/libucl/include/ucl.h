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

#ifndef UCL_H_
#define UCL_H_

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

/**
 * @mainpage
 * This is a reference manual for UCL API. You may find the description of UCL format by following this
 * [github repository](https://github.com/vstakhov/libucl).
 *
 * This manual has several main sections:
 *  - @ref structures
 *  - @ref utils
 *  - @ref parser
 *  - @ref emitter
 */

/**
 * @file ucl.h
 * @brief UCL parsing and emitting functions
 *
 * UCL is universal configuration language, which is a form of
 * JSON with less strict rules that make it more comfortable for
 * using as a configuration language
 */
#ifdef  __cplusplus
extern "C" {
#endif
/*
 * Memory allocation utilities
 * UCL_ALLOC(size) - allocate memory for UCL
 * UCL_FREE(size, ptr) - free memory of specified size at ptr
 * Default: malloc and free
 */
#ifndef UCL_ALLOC
#define UCL_ALLOC(size) malloc(size)
#endif
#ifndef UCL_FREE
#define UCL_FREE(size, ptr) free(ptr)
#endif

#if    __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
#define UCL_WARN_UNUSED_RESULT               \
  __attribute__((warn_unused_result))
#else
#define UCL_WARN_UNUSED_RESULT
#endif

/**
 * @defgroup structures Structures and types
 * UCL defines several enumeration types used for error reporting or specifying flags and attributes.
 *
 * @{
 */

/**
 * The common error codes returned by ucl parser
 */
typedef enum ucl_error {
	UCL_EOK = 0, /**< No error */
	UCL_ESYNTAX, /**< Syntax error occurred during parsing */
	UCL_EIO, /**< IO error occurred during parsing */
	UCL_ESTATE, /**< Invalid state machine state */
	UCL_ENESTED, /**< Input has too many recursion levels */
	UCL_EMACRO, /**< Error processing a macro */
	UCL_EINTERNAL, /**< Internal unclassified error */
	UCL_ESSL /**< SSL error */
} ucl_error_t;

/**
 * #ucl_object_t may have one of specified types, some types are compatible with each other and some are not.
 * For example, you can always convert #UCL_TIME to #UCL_FLOAT. Also you can convert #UCL_FLOAT to #UCL_INTEGER
 * by loosing floating point. Every object may be converted to a string by #ucl_object_tostring_forced() function.
 *
 */
typedef enum ucl_type {
	UCL_OBJECT = 0, /**< UCL object - key/value pairs */
	UCL_ARRAY, /**< UCL array */
	UCL_INT, /**< Integer number */
	UCL_FLOAT, /**< Floating point number */
	UCL_STRING, /**< Null terminated string */
	UCL_BOOLEAN, /**< Boolean value */
	UCL_TIME, /**< Time value (floating point number of seconds) */
	UCL_USERDATA, /**< Opaque userdata pointer (may be used in macros) */
	UCL_NULL /**< Null value */
} ucl_type_t;

/**
 * You can use one of these types to serialise #ucl_object_t by using ucl_object_emit().
 */
typedef enum ucl_emitter {
	UCL_EMIT_JSON = 0, /**< Emit fine formatted JSON */
	UCL_EMIT_JSON_COMPACT, /**< Emit compacted JSON */
	UCL_EMIT_CONFIG, /**< Emit human readable config format */
	UCL_EMIT_YAML /**< Emit embedded YAML format */
} ucl_emitter_t;

/**
 * These flags defines parser behaviour. If you specify #UCL_PARSER_ZEROCOPY you must ensure
 * that the input memory is not freed if an object is in use. Moreover, if you want to use
 * zero-terminated keys and string values then you should not use zero-copy mode, as in this case
 * UCL still has to perform copying implicitly.
 */
typedef enum ucl_parser_flags {
	UCL_PARSER_KEY_LOWERCASE = 0x1, /**< Convert all keys to lower case */
	UCL_PARSER_ZEROCOPY = 0x2 /**< Parse input in zero-copy mode if possible */
} ucl_parser_flags_t;

/**
 * String conversion flags, that are used in #ucl_object_fromstring_common function.
 */
typedef enum ucl_string_flags {
	UCL_STRING_ESCAPE = 0x1,  /**< Perform JSON escape */
	UCL_STRING_TRIM = 0x2,    /**< Trim leading and trailing whitespaces */
	UCL_STRING_PARSE_BOOLEAN = 0x4,    /**< Parse passed string and detect boolean */
	UCL_STRING_PARSE_INT = 0x8,    /**< Parse passed string and detect integer number */
	UCL_STRING_PARSE_DOUBLE = 0x10,    /**< Parse passed string and detect integer or float number */
	UCL_STRING_PARSE_NUMBER =  UCL_STRING_PARSE_INT|UCL_STRING_PARSE_DOUBLE ,  /**<
									Parse passed string and detect number */
	UCL_STRING_PARSE =  UCL_STRING_PARSE_BOOLEAN|UCL_STRING_PARSE_NUMBER,   /**<
									Parse passed string (and detect booleans and numbers) */
	UCL_STRING_PARSE_BYTES = 0x20  /**< Treat numbers as bytes */
} ucl_string_flags_t;

/**
 * Basic flags for an object
 */
typedef enum ucl_object_flags {
	UCL_OBJECT_ALLOCATED_KEY = 1, /**< An object has key allocated internally */
	UCL_OBJECT_ALLOCATED_VALUE = 2, /**< An object has a string value allocated internally */
	UCL_OBJECT_NEED_KEY_ESCAPE = 4 /**< The key of an object need to be escaped on output */
} ucl_object_flags_t;

/**
 * UCL object structure. Please mention that the most of fields should not be touched by
 * UCL users. In future, this structure may be converted to private one.
 */
typedef struct ucl_object_s {
	/**
	 * Variant value type
	 */
	union {
		int64_t iv;							/**< Int value of an object */
		const char *sv;					/**< String value of an object */
		double dv;							/**< Double value of an object */
		struct ucl_object_s *av;			/**< Array					*/
		void *ov;							/**< Object					*/
		void* ud;							/**< Opaque user data		*/
	} value;
	const char *key;						/**< Key of an object		*/
	struct ucl_object_s *next;				/**< Array handle			*/
	struct ucl_object_s *prev;				/**< Array handle			*/
	unsigned char* trash_stack[2];			/**< Pointer to allocated chunks */
	unsigned keylen;						/**< Lenght of a key		*/
	unsigned len;							/**< Size of an object		*/
	enum ucl_type type;						/**< Real type				*/
	uint16_t ref;							/**< Reference count		*/
	uint16_t flags;							/**< Object flags			*/
} ucl_object_t;

/** @} */

/**
 * @defgroup utils Utility functions
 * A number of utility functions simplify handling of UCL objects
 *
 * @{
 */
/**
 * Copy and return a key of an object, returned key is zero-terminated
 * @param obj CL object
 * @return zero terminated key
 */
char* ucl_copy_key_trash (ucl_object_t *obj);

/**
 * Copy and return a string value of an object, returned key is zero-terminated
 * @param obj CL object
 * @return zero terminated string representation of object value
 */
char* ucl_copy_value_trash (ucl_object_t *obj);

/**
 * Creates a new object
 * @return new object
 */
static inline ucl_object_t* ucl_object_new (void) UCL_WARN_UNUSED_RESULT;
static inline ucl_object_t *
ucl_object_new (void)
{
	ucl_object_t *new;
	new = malloc (sizeof (ucl_object_t));
	if (new != NULL) {
		memset (new, 0, sizeof (ucl_object_t));
		new->ref = 1;
		new->type = UCL_NULL;
	}
	return new;
}

/**
 * Create new object with type specified
 * @param type type of a new object
 * @return new object
 */
static inline ucl_object_t* ucl_object_typed_new (unsigned int type) UCL_WARN_UNUSED_RESULT;
static inline ucl_object_t *
ucl_object_typed_new (unsigned int type)
{
	ucl_object_t *new;
	new = malloc (sizeof (ucl_object_t));
	if (new != NULL) {
		memset (new, 0, sizeof (ucl_object_t));
		new->ref = 1;
		new->type = (type <= UCL_NULL ? type : UCL_NULL);
	}
	return new;
}

/**
 * Convert any string to an ucl object making the specified transformations
 * @param str fixed size or NULL terminated string
 * @param len length (if len is zero, than str is treated as NULL terminated)
 * @param flags conversion flags
 * @return new object
 */
ucl_object_t * ucl_object_fromstring_common (const char *str, size_t len,
		enum ucl_string_flags flags) UCL_WARN_UNUSED_RESULT;

/**
 * Create a UCL object from the specified string
 * @param str NULL terminated string, will be json escaped
 * @return new object
 */
static inline ucl_object_t *
ucl_object_fromstring (const char *str)
{
	return ucl_object_fromstring_common (str, 0, UCL_STRING_ESCAPE);
}

/**
 * Create a UCL object from the specified string
 * @param str fixed size string, will be json escaped
 * @param len length of a string
 * @return new object
 */
static inline ucl_object_t *
ucl_object_fromlstring (const char *str, size_t len)
{
	return ucl_object_fromstring_common (str, len, UCL_STRING_ESCAPE);
}

/**
 * Create an object from an integer number
 * @param iv number
 * @return new object
 */
static inline ucl_object_t *
ucl_object_fromint (int64_t iv)
{
	ucl_object_t *obj;

	obj = ucl_object_new ();
	if (obj != NULL) {
		obj->type = UCL_INT;
		obj->value.iv = iv;
	}

	return obj;
}

/**
 * Create an object from a float number
 * @param dv number
 * @return new object
 */
static inline ucl_object_t *
ucl_object_fromdouble (double dv)
{
	ucl_object_t *obj;

	obj = ucl_object_new ();
	if (obj != NULL) {
		obj->type = UCL_FLOAT;
		obj->value.dv = dv;
	}

	return obj;
}

/**
 * Create an object from a boolean
 * @param bv bool value
 * @return new object
 */
static inline ucl_object_t *
ucl_object_frombool (bool bv)
{
	ucl_object_t *obj;

	obj = ucl_object_new ();
	if (obj != NULL) {
		obj->type = UCL_BOOLEAN;
		obj->value.iv = bv;
	}

	return obj;
}

/**
 * Insert a object 'elt' to the hash 'top' and associate it with key 'key'
 * @param top destination object (will be created automatically if top is NULL)
 * @param elt element to insert (must NOT be NULL)
 * @param key key to associate with this object (either const or preallocated)
 * @param keylen length of the key (or 0 for NULL terminated keys)
 * @param copy_key make an internal copy of key
 * @return new value of top object
 */
ucl_object_t* ucl_object_insert_key (ucl_object_t *top, ucl_object_t *elt,
		const char *key, size_t keylen, bool copy_key) UCL_WARN_UNUSED_RESULT;

/**
 * Replace a object 'elt' to the hash 'top' and associate it with key 'key', old object will be unrefed,
 * if no object has been found this function works like ucl_object_insert_key()
 * @param top destination object (will be created automatically if top is NULL)
 * @param elt element to insert (must NOT be NULL)
 * @param key key to associate with this object (either const or preallocated)
 * @param keylen length of the key (or 0 for NULL terminated keys)
 * @param copy_key make an internal copy of key
 * @return new value of top object
 */
ucl_object_t* ucl_object_replace_key (ucl_object_t *top, ucl_object_t *elt,
		const char *key, size_t keylen, bool copy_key) UCL_WARN_UNUSED_RESULT;

/**
 * Insert a object 'elt' to the hash 'top' and associate it with key 'key', if the specified key exist,
 * try to merge its content
 * @param top destination object (will be created automatically if top is NULL)
 * @param elt element to insert (must NOT be NULL)
 * @param key key to associate with this object (either const or preallocated)
 * @param keylen length of the key (or 0 for NULL terminated keys)
 * @param copy_key make an internal copy of key
 * @return new value of top object
 */
ucl_object_t* ucl_object_insert_key_merged (ucl_object_t *top, ucl_object_t *elt,
		const char *key, size_t keylen, bool copy_key) UCL_WARN_UNUSED_RESULT;

/**
 * Append an element to the front of array object
 * @param top destination object (will be created automatically if top is NULL)
 * @param elt element to append (must NOT be NULL)
 * @return new value of top object
 */
static inline ucl_object_t * ucl_array_append (ucl_object_t *top,
		ucl_object_t *elt) UCL_WARN_UNUSED_RESULT;
static inline ucl_object_t *
ucl_array_append (ucl_object_t *top, ucl_object_t *elt)
{
	ucl_object_t *head;

	if (elt == NULL) {
		return NULL;
	}

	if (top == NULL) {
		top = ucl_object_typed_new (UCL_ARRAY);
		top->value.av = elt;
		elt->next = NULL;
		elt->prev = elt;
		top->len = 1;
	}
	else {
		head = top->value.av;
		if (head == NULL) {
			top->value.av = elt;
			elt->prev = elt;
		}
		else {
			elt->prev = head->prev;
			head->prev->next = elt;
			head->prev = elt;
		}
		elt->next = NULL;
		top->len ++;
	}

	return top;
}

/**
 * Append an element to the start of array object
 * @param top destination object (will be created automatically if top is NULL)
 * @param elt element to append (must NOT be NULL)
 * @return new value of top object
 */
static inline ucl_object_t * ucl_array_prepend (ucl_object_t *top,
		ucl_object_t *elt) UCL_WARN_UNUSED_RESULT;
static inline ucl_object_t *
ucl_array_prepend (ucl_object_t *top, ucl_object_t *elt)
{
	ucl_object_t *head;

	if (elt == NULL) {
		return NULL;
	}

	if (top == NULL) {
		top = ucl_object_typed_new (UCL_ARRAY);
		top->value.av = elt;
		elt->next = NULL;
		elt->prev = elt;
		top->len = 1;
	}
	else {
		head = top->value.av;
		if (head == NULL) {
			top->value.av = elt;
			elt->prev = elt;
		}
		else {
			elt->prev = head->prev;
			head->prev = elt;
		}
		elt->next = head;
		top->value.av = elt;
		top->len ++;
	}

	return top;
}

/**
 * Removes an element `elt` from the array `top`. Caller must unref the returned object when it is not
 * needed.
 * @param top array ucl object
 * @param elt element to remove
 * @return removed element or NULL if `top` is NULL or not an array
 */
static inline ucl_object_t *
ucl_array_delete (ucl_object_t *top, ucl_object_t *elt)
{
	ucl_object_t *head;

	if (top == NULL || top->type != UCL_ARRAY || top->value.av == NULL) {
		return NULL;
	}
	head = top->value.av;

	if (elt->prev == elt) {
		top->value.av = NULL;
	}
	else if (elt == head) {
		elt->next->prev = elt->prev;
		top->value.av = elt->next;
	}
	else {
		elt->prev->next = elt->next;
		if (elt->next) {
			elt->next->prev = elt->prev;
		}
		else {
			head->prev = elt->prev;
		}
	}
	elt->next = NULL;
	elt->prev = elt;
	top->len --;

	return elt;
}

/**
 * Returns the first element of the array `top`
 * @param top array ucl object
 * @return element or NULL if `top` is NULL or not an array
 */
static inline ucl_object_t *
ucl_array_head (ucl_object_t *top)
{
	if (top == NULL || top->type != UCL_ARRAY || top->value.av == NULL) {
		return NULL;
	}
	return top->value.av;
}

/**
 * Returns the last element of the array `top`
 * @param top array ucl object
 * @return element or NULL if `top` is NULL or not an array
 */
static inline ucl_object_t *
ucl_array_tail (ucl_object_t *top)
{
	if (top == NULL || top->type != UCL_ARRAY || top->value.av == NULL) {
		return NULL;
	}
	return top->value.av->prev;
}

/**
 * Removes the last element from the array `top`. Caller must unref the returned object when it is not
 * needed.
 * @param top array ucl object
 * @return removed element or NULL if `top` is NULL or not an array
 */
static inline ucl_object_t *
ucl_array_pop_last (ucl_object_t *top)
{
	return ucl_array_delete (top, ucl_array_tail (top));
}

/**
 * Removes the first element from the array `top`. Caller must unref the returned object when it is not
 * needed.
 * @param top array ucl object
 * @return removed element or NULL if `top` is NULL or not an array
 */
static inline ucl_object_t *
ucl_array_pop_first (ucl_object_t *top)
{
	return ucl_array_delete (top, ucl_array_head (top));
}

/**
 * Append a element to another element forming an implicit array
 * @param head head to append (may be NULL)
 * @param elt new element
 * @return new head if applicable
 */
static inline ucl_object_t * ucl_elt_append (ucl_object_t *head,
		ucl_object_t *elt) UCL_WARN_UNUSED_RESULT;
static inline ucl_object_t *
ucl_elt_append (ucl_object_t *head, ucl_object_t *elt)
{

	if (head == NULL) {
		elt->next = NULL;
		elt->prev = elt;
		head = elt;
	}
	else {
		elt->prev = head->prev;
		head->prev->next = elt;
		head->prev = elt;
		elt->next = NULL;
	}

	return head;
}

/**
 * Converts an object to double value
 * @param obj CL object
 * @param target target double variable
 * @return true if conversion was successful
 */
static inline bool
ucl_object_todouble_safe (ucl_object_t *obj, double *target)
{
	if (obj == NULL) {
		return false;
	}
	switch (obj->type) {
	case UCL_INT:
		*target = obj->value.iv; /* Probaly could cause overflow */
		break;
	case UCL_FLOAT:
	case UCL_TIME:
		*target = obj->value.dv;
		break;
	default:
		return false;
	}

	return true;
}

/**
 * Unsafe version of \ref ucl_obj_todouble_safe
 * @param obj CL object
 * @return double value
 */
static inline double
ucl_object_todouble (ucl_object_t *obj)
{
	double result = 0.;

	ucl_object_todouble_safe (obj, &result);
	return result;
}

/**
 * Converts an object to integer value
 * @param obj CL object
 * @param target target integer variable
 * @return true if conversion was successful
 */
static inline bool
ucl_object_toint_safe (ucl_object_t *obj, int64_t *target)
{
	if (obj == NULL) {
		return false;
	}
	switch (obj->type) {
	case UCL_INT:
		*target = obj->value.iv;
		break;
	case UCL_FLOAT:
	case UCL_TIME:
		*target = obj->value.dv; /* Loosing of decimal points */
		break;
	default:
		return false;
	}

	return true;
}

/**
 * Unsafe version of \ref ucl_obj_toint_safe
 * @param obj CL object
 * @return int value
 */
static inline int64_t
ucl_object_toint (ucl_object_t *obj)
{
	int64_t result = 0;

	ucl_object_toint_safe (obj, &result);
	return result;
}

/**
 * Converts an object to boolean value
 * @param obj CL object
 * @param target target boolean variable
 * @return true if conversion was successful
 */
static inline bool
ucl_object_toboolean_safe (ucl_object_t *obj, bool *target)
{
	if (obj == NULL) {
		return false;
	}
	switch (obj->type) {
	case UCL_BOOLEAN:
		*target = (obj->value.iv == true);
		break;
	default:
		return false;
	}

	return true;
}

/**
 * Unsafe version of \ref ucl_obj_toboolean_safe
 * @param obj CL object
 * @return boolean value
 */
static inline bool
ucl_object_toboolean (ucl_object_t *obj)
{
	bool result = false;

	ucl_object_toboolean_safe (obj, &result);
	return result;
}

/**
 * Converts an object to string value
 * @param obj CL object
 * @param target target string variable, no need to free value
 * @return true if conversion was successful
 */
static inline bool
ucl_object_tostring_safe (ucl_object_t *obj, const char **target)
{
	if (obj == NULL) {
		return false;
	}

	switch (obj->type) {
	case UCL_STRING:
		*target = ucl_copy_value_trash (obj);
		break;
	default:
		return false;
	}

	return true;
}

/**
 * Unsafe version of \ref ucl_obj_tostring_safe
 * @param obj CL object
 * @return string value
 */
static inline const char *
ucl_object_tostring (ucl_object_t *obj)
{
	const char *result = NULL;

	ucl_object_tostring_safe (obj, &result);
	return result;
}

/**
 * Convert any object to a string in JSON notation if needed
 * @param obj CL object
 * @return string value
 */
static inline const char *
ucl_object_tostring_forced (ucl_object_t *obj)
{
	return ucl_copy_value_trash (obj);
}

/**
 * Return string as char * and len, string may be not zero terminated, more efficient that \ref ucl_obj_tostring as it
 * allows zero-copy (if #UCL_PARSER_ZEROCOPY has been used during parsing)
 * @param obj CL object
 * @param target target string variable, no need to free value
 * @param tlen target length
 * @return true if conversion was successful
 */
static inline bool
ucl_object_tolstring_safe (ucl_object_t *obj, const char **target, size_t *tlen)
{
	if (obj == NULL) {
		return false;
	}
	switch (obj->type) {
	case UCL_STRING:
		*target = obj->value.sv;
		*tlen = obj->len;
		break;
	default:
		return false;
	}

	return true;
}

/**
 * Unsafe version of \ref ucl_obj_tolstring_safe
 * @param obj CL object
 * @return string value
 */
static inline const char *
ucl_object_tolstring (ucl_object_t *obj, size_t *tlen)
{
	const char *result = NULL;

	ucl_object_tolstring_safe (obj, &result, tlen);
	return result;
}

/**
 * Return object identified by a key in the specified object
 * @param obj object to get a key from (must be of type UCL_OBJECT)
 * @param key key to search
 * @return object matched the specified key or NULL if key is not found
 */
ucl_object_t * ucl_object_find_key (ucl_object_t *obj, const char *key);

/**
 * Return object identified by a fixed size key in the specified object
 * @param obj object to get a key from (must be of type UCL_OBJECT)
 * @param key key to search
 * @param klen length of a key
 * @return object matched the specified key or NULL if key is not found
 */
ucl_object_t *ucl_object_find_keyl (ucl_object_t *obj, const char *key, size_t klen);

/**
 * Returns a key of an object as a NULL terminated string
 * @param obj CL object
 * @return key or NULL if there is no key
 */
static inline const char *
ucl_object_key (ucl_object_t *obj)
{
	return ucl_copy_key_trash (obj);
}

/**
 * Returns a key of an object as a fixed size string (may be more efficient)
 * @param obj CL object
 * @param len target key length
 * @return key pointer
 */
static inline const char *
ucl_object_keyl (ucl_object_t *obj, size_t *len)
{
	*len = obj->keylen;
	return obj->key;
}

/**
 * Free ucl object
 * @param obj ucl object to free
 */
void ucl_object_free (ucl_object_t *obj);

/**
 * Increase reference count for an object
 * @param obj object to ref
 */
static inline ucl_object_t *
ucl_object_ref (ucl_object_t *obj) {
	obj->ref ++;
	return obj;
}

/**
 * Decrease reference count for an object
 * @param obj object to unref
 */
static inline void
ucl_object_unref (ucl_object_t *obj) {
	if (obj != NULL && --obj->ref <= 0) {
		ucl_object_free (obj);
	}
}
/**
 * Opaque iterator object
 */
typedef void* ucl_object_iter_t;

/**
 * Get next key from an object
 * @param obj object to iterate
 * @param iter opaque iterator, must be set to NULL on the first call:
 * ucl_object_iter_t it = NULL;
 * while ((cur = ucl_iterate_object (obj, &it)) != NULL) ...
 * @return the next object or NULL
 */
ucl_object_t* ucl_iterate_object (ucl_object_t *obj, ucl_object_iter_t *iter, bool expand_values);
/** @} */


/**
 * @defgroup parser Parsing functions
 * These functions are used to parse UCL objects
 *
 * @{
 */

/**
 * Macro handler for a parser
 * @param data the content of macro
 * @param len the length of content
 * @param ud opaque user data
 * @param err error pointer
 * @return true if macro has been parsed
 */
typedef bool (*ucl_macro_handler) (const unsigned char *data, size_t len, void* ud);

/* Opaque parser */
struct ucl_parser;

/**
 * Creates new parser object
 * @param pool pool to allocate memory from
 * @return new parser object
 */
struct ucl_parser* ucl_parser_new (int flags);

/**
 * Register new handler for a macro
 * @param parser parser object
 * @param macro macro name (without leading dot)
 * @param handler handler (it is called immediately after macro is parsed)
 * @param ud opaque user data for a handler
 */
void ucl_parser_register_macro (struct ucl_parser *parser, const char *macro,
		ucl_macro_handler handler, void* ud);

/**
 * Register new parser variable
 * @param parser parser object
 * @param var variable name
 * @param value variable value
 */
void ucl_parser_register_variable (struct ucl_parser *parser, const char *var,
		const char *value);

/**
 * Load new chunk to a parser
 * @param parser parser structure
 * @param data the pointer to the beginning of a chunk
 * @param len the length of a chunk
 * @param err if *err is NULL it is set to parser error
 * @return true if chunk has been added and false in case of error
 */
bool ucl_parser_add_chunk (struct ucl_parser *parser, const unsigned char *data, size_t len);

/**
 * Load and add data from a file
 * @param parser parser structure
 * @param filename the name of file
 * @param err if *err is NULL it is set to parser error
 * @return true if chunk has been added and false in case of error
 */
bool ucl_parser_add_file (struct ucl_parser *parser, const char *filename);

/**
 * Get a top object for a parser
 * @param parser parser structure
 * @param err if *err is NULL it is set to parser error
 * @return top parser object or NULL
 */
ucl_object_t* ucl_parser_get_object (struct ucl_parser *parser);

/**
 * Get the error string if failing
 * @param parser parser object
 */
const char *ucl_parser_get_error(struct ucl_parser *parser);
/**
 * Free ucl parser object
 * @param parser parser object
 */
void ucl_parser_free (struct ucl_parser *parser);

/**
 * Add new public key to parser for signatures check
 * @param parser parser object
 * @param key PEM representation of a key
 * @param len length of the key
 * @param err if *err is NULL it is set to parser error
 * @return true if a key has been successfully added
 */
bool ucl_pubkey_add (struct ucl_parser *parser, const unsigned char *key, size_t len);

/**
 * Set FILENAME and CURDIR variables in parser
 * @param parser parser object
 * @param filename filename to set or NULL to set FILENAME to "undef" and CURDIR to getcwd()
 * @param need_expand perform realpath() if this variable is true and filename is not NULL
 * @return true if variables has been set
 */
bool ucl_parser_set_filevars (struct ucl_parser *parser, const char *filename,
		bool need_expand);

/** @} */

/**
 * @defgroup emitter Emitting functions
 * These functions are used to serialise UCL objects to some string representation.
 *
 * @{
 */

/**
 * Structure using for emitter callbacks
 */
struct ucl_emitter_functions {
	/** Append a single character */
	int (*ucl_emitter_append_character) (unsigned char c, size_t nchars, void *ud);
	/** Append a string of a specified length */
	int (*ucl_emitter_append_len) (unsigned const char *str, size_t len, void *ud);
	/** Append a 64 bit integer */
	int (*ucl_emitter_append_int) (int64_t elt, void *ud);
	/** Append floating point element */
	int (*ucl_emitter_append_double) (double elt, void *ud);
	/** Opaque userdata pointer */
	void *ud;
};

/**
 * Emit object to a string
 * @param obj object
 * @param emit_type if type is #UCL_EMIT_JSON then emit json, if type is
 * #UCL_EMIT_CONFIG then emit config like object
 * @return dump of an object (must be freed after using) or NULL in case of error
 */
unsigned char *ucl_object_emit (ucl_object_t *obj, enum ucl_emitter emit_type);

/**
 * Emit object to a string
 * @param obj object
 * @param emit_type if type is #UCL_EMIT_JSON then emit json, if type is
 * #UCL_EMIT_CONFIG then emit config like object
 * @return dump of an object (must be freed after using) or NULL in case of error
 */
bool ucl_object_emit_full (ucl_object_t *obj, enum ucl_emitter emit_type,
		struct ucl_emitter_functions *emitter);
/** @} */

#ifdef  __cplusplus
}
#endif
/*
 * XXX: Poorly named API functions, need to replace them with the appropriate
 * named function. All API functions *must* use naming ucl_object_*. Usage of
 * ucl_obj* should be avoided.
 */
#define ucl_obj_todouble_safe ucl_object_todouble_safe
#define ucl_obj_todouble ucl_object_todouble
#define ucl_obj_tostring ucl_object_tostring
#define ucl_obj_tostring_safe ucl_object_tostring_safe
#define ucl_obj_tolstring ucl_object_tolstring
#define ucl_obj_tolstring_safe ucl_object_tolstring_safe
#define ucl_obj_toint ucl_object_toint
#define ucl_obj_toint_safe ucl_object_toint_safe
#define ucl_obj_toboolean ucl_object_toboolean
#define ucl_obj_toboolean_safe ucl_object_toboolean_safe
#define ucl_obj_get_key ucl_object_find_key
#define ucl_obj_get_keyl ucl_object_find_keyl
#define ucl_obj_unref ucl_object_unref
#define ucl_obj_ref ucl_object_ref
#define ucl_obj_free ucl_object_free

#endif /* UCL_H_ */
