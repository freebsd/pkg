/*-
 * Copyright (c) 2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * All rights reserved.
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

#include <assert.h>
#include <ucl.h>
#include "pkg.h"
#include "private/pkg.h"

const char *
pkg_object_dump(const pkg_object *o)
{
	if (o == NULL)
		return ("");

	return (ucl_object_emit(o, UCL_EMIT_CONFIG));
}

void
pkg_object_free(pkg_object *o)
{
	ucl_object_unref(o);
}

const char *
pkg_object_key(const pkg_object *o)
{
	if (o == NULL)
		return (NULL);

	return (ucl_object_key(o));
}

const pkg_object *
pkg_object_iterate(const pkg_object *o, pkg_iter *it)
{
	if (o == NULL)
		return (NULL);

	return (ucl_iterate_object(o, it, true));
}

pkg_object_t
pkg_object_type(const pkg_object *o)
{

	if (o == NULL)
		return (PKG_NULL);

	switch (o->type) {
	case UCL_OBJECT:
		return (PKG_OBJECT);
	case UCL_BOOLEAN:
		return (PKG_BOOL);
	case UCL_STRING:
		return (PKG_STRING);
	case UCL_INT:
		return (PKG_INT);
	case UCL_ARRAY:
		return (PKG_ARRAY);
	default:
		return (PKG_NULL);
	};

}

bool
pkg_object_bool(const pkg_object *o)
{
	if (o == NULL || o->type != UCL_BOOLEAN)
		return (false);

	return (ucl_object_toboolean(o));
}

const char *
pkg_object_string(const pkg_object *o)
{
	const char *ret;

	if (o == NULL)
		return (NULL);

	ret = ucl_object_tostring_forced(o);

	if (ret && *ret == '\0')
		return (NULL);
	return (ret);
}

int64_t
pkg_object_int(const pkg_object *o)
{
	if (o == NULL || o->type != UCL_INT)
		return (0);

	return (ucl_object_toint(o));
}

unsigned
pkg_object_count(const pkg_object *o)
{
	return (UCL_COUNT(o));
}

const pkg_object *
pkg_object_find(const pkg_object *o, const char *key)
{
	if (o == NULL)
		return (NULL);

	return (ucl_object_find_key(o, key));
}
