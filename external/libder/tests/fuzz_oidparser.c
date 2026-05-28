/*-
 * Copyright (c) 2026 Kyle Evans <kevans@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/param.h>
#include <sys/socket.h>

#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libder.h>

#include "fuzzers.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t sz)
{
	struct libder_ctx *ctx;
	struct libder_object *obj;
	uint32_t root, sec;

	/* Looking for C-style strings here. */
	if (sz == 0 || data[sz - 1] != '\0' || memchr(data, 0, sz - 1) != NULL)
		return (-1);

	/*
	 * Let's accept only canonically-valid OIDs; only roots 0, 1, and 2 exist,
	 * and roots 0/1 are constrainted to 0-39.  Root 2 is unbounded.
	 */
	if (sscanf((const char *)data, "%d.%d", &root, &sec) != 2 || root > 2)
		return (-1);
	if (root < 2 && sec > 39)
		return (-1);

	ctx = libder_open();
	obj = libder_obj_alloc_oid(ctx, (const char *)data);
	if (obj == NULL) {
		libder_close(ctx);
		return (-1);
	}

	libder_obj_free(obj);
	libder_close(ctx);
	return (0);
}
