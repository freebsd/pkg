/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for trigger UCL parsing + schema validation.
 * Tests the same code paths as trigger_load() (which is static).
 */

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"

static const char trigger_schema_str[] = ""
	"{"
	"  type = object;"
	"  properties {"
	"    description: { type = string };"
	"    path: { "
	"      anyOf = [{"
	"        type = array; "
	"        item = { type = string };"
	"      }, {"
	"        type = string;"
	"      }]"
	"    };"
	"    path_glob: { "
	"      anyOf = [{"
	"        type = array; "
	"        item = { type = string };"
	"      }, {"
	"        type = string;"
	"      }]"
	"    };"
	"    path_regexp: { "
	"      anyOf = [{"
	"        type = array; "
	"        item = { type = string };"
	"      }, {"
	"        type = string;"
	"      }]"
	"    };"
	"    cleanup = { "
	"      type = object; "
	"      properties = {"
	"        type = { "
	"          type = string,"
	"          sandbox = boolean, "
	"          enum: [lua];"
	"        };"
	"        script = { type = string };"
	"      }; "
	"      required = [ type, script ];"
	"    };"
	"    trigger = { "
	"      type = object; "
	"      properties = {"
	"        type = { "
	"          type = string,"
	"          sandbox = boolean, "
	"          enum: [lua];"
	"        };"
	"        script = { type = string };"
	"      }; "
	"      required = [ type, script ];"
	"    };"
	"  }\n"
	"  required = [ trigger ];"
	"}";

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char tmpdir[] = "/tmp/pkg_fuzz_trigger.XXXXXX";
	char tmpfile[PATH_MAX];
	int fd, dfd;
	ucl_object_t *obj, *schema;
	struct ucl_schema_error err;

	if (mkdtemp(tmpdir) == NULL)
		return (0);

	snprintf(tmpfile, sizeof(tmpfile), "%s/fuzz.ucl", tmpdir);
	fd = open(tmpfile, O_CREAT | O_WRONLY, 0644);
	if (fd < 0) {
		(void)rmdir(tmpdir);
		return (0);
	}

	if (write(fd, data, size) != (ssize_t)size) {
		close(fd);
		(void)unlink(tmpfile);
		(void)rmdir(tmpdir);
		return (0);
	}
	close(fd);

	dfd = open(tmpdir, O_DIRECTORY);
	if (dfd < 0) {
		(void)unlink(tmpfile);
		(void)rmdir(tmpdir);
		return (0);
	}

	fd = openat(dfd, "fuzz.ucl", O_RDONLY);
	if (fd < 0) {
		close(dfd);
		(void)unlink(tmpfile);
		(void)rmdir(tmpdir);
		return (0);
	}

	obj = ucl_parse_fd(fd, "fuzz.ucl");
	close(fd);
	close(dfd);

	if (obj != NULL) {
		schema = ucl_parse_buf(trigger_schema_str,
		    sizeof(trigger_schema_str) - 1, "trigger schema");
		if (schema != NULL) {
			(void)ucl_object_validate(schema, obj, &err);
			ucl_object_unref(schema);
		}
		ucl_object_unref(obj);
	}

	(void)unlink(tmpfile);
	(void)rmdir(tmpdir);
	return (0);
}
