/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for Lua script UCL parsing.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ucl.h>
#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct pkg *pkg = NULL;
	struct ucl_parser *parser;
	ucl_object_t *obj;

	if (size == 0)
		return (0);

	parser = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (parser == NULL)
		return (0);

	if (!ucl_parser_add_chunk(parser, data, size)) {
		ucl_parser_free(parser);
		return (0);
	}

	obj = ucl_parser_get_object(parser);
	ucl_parser_free(parser);

	if (obj == NULL)
		return (0);

	if (pkg_new(&pkg, PKG_INSTALLED) != EPKG_OK) {
		ucl_object_unref(obj);
		return (0);
	}

	for (int i = 0; i < PKG_NUM_LUA_SCRIPTS; i++)
		pkg_lua_script_from_ucl(pkg, obj, i);

	pkg_free(pkg);
	ucl_object_unref(obj);

	return (0);
}
