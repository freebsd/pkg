/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for keyword UCL parsing + schema validation.
 * Tests the same code paths as external_keyword() (which is static).
 */

#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"

static const char keyword_schema_str[] = ""
	"{"
	"  type = object;"
	"  properties {"
	"    actions = { "
	"      type = array; "
	"      items = { type = string }; "
	"      uniqueItems: true "
	"    }; "
	"    actions_script = { type = string }; "
	"    arguments = { type = boolean }; "
	"    preformat_arguments = { type = boolean }; "
	"    prepackaging = { type = string }; "
	"    deprecated = { type = boolean }; "
	"    deprecation_message = { type = string }; "
	"    attributes = { "
	"      type = object; "
	"      properties { "
	"        owner = { type = string }; "
	"        group = { type = string }; "
	"        mode = { oneOf: [ { type = integer }, { type = string } ] }; "
	"      }"
	"    }; "
	"    pre-install = { type = string }; "
	"    post-install = { type = string }; "
	"    pre-deinstall = { type = string }; "
	"    post-deinstall = { type = string }; "
	"    pre-install-lua = { type = string }; "
	"    post-install-lua = { type = string }; "
	"    pre-deinstall-lua = { type = string }; "
	"    post-deinstall-lua = { type = string }; "
	"    messages: {"
	"        type = array; "
	"        items = {"
	"            type = object;"
	"            properties {"
	"                message = { type = string };"
	"                type = { enum = [ upgrade, remove, install ] };"
	"            };"
	"            required [ message ];"
	"        };"
	"    };"
	"  }"
	"}";

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char tmp[] = "/tmp/pkg_fuzz.XXXXXX";
	int fd, pfd;
	struct ucl_parser *p;
	ucl_object_t *obj, *schema;
	struct ucl_schema_error err;

	fd = mkstemp(tmp);
	if (fd < 0)
		return (0);

	if (write(fd, data, size) != (ssize_t)size) {
		close(fd);
		(void)unlink(tmp);
		return (0);
	}
	close(fd);

	pfd = open(tmp, O_RDONLY);
	if (pfd < 0) {
		(void)unlink(tmp);
		return (0);
	}

	p = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_fd(p, pfd)) {
		ucl_parser_free(p);
		close(pfd);
		(void)unlink(tmp);
		return (0);
	}
	close(pfd);

	obj = ucl_parser_get_object(p);
	ucl_parser_free(p);

	if (obj != NULL) {
		schema = ucl_parse_buf(keyword_schema_str,
		    sizeof(keyword_schema_str) - 1, "keyword schema");
		if (schema != NULL) {
			(void)ucl_object_validate(schema, obj, &err);
			ucl_object_unref(schema);
		}
		ucl_object_unref(obj);
	}

	(void)unlink(tmp);
	return (0);
}
