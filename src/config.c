/*-
 * Copyright (c) 2013 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <ctype.h>
#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <sysexits.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_config(void)
{
	fprintf(stderr,
            "Usage: pkg config <name>\n\n");
	//fprintf(stderr, "For more information see 'pkg help config'.\n");
}

int
exec_config(int argc, char **argv)
{
	const pkg_object *conf, *o;
	pkg_iter it = NULL;
	const char *buf;
	char *key;
	int64_t integer;
	int i;
	bool b;

	if (argc != 2) {
		usage_config();
		return (EX_USAGE);
	}

	key = argv[1];
	for (i = 0; key[i] != '\0'; i++)
		key[i] = toupper(key[i]);

	conf = pkg_config_get(key);
	if (conf == NULL) {
		warnx("No such configuration options: %s", key);
		return (EX_SOFTWARE);
	}

	switch (pkg_object_type(conf)) {
	case PKG_STRING:
		buf = pkg_object_string(conf);
		printf("%s\n", buf == NULL ? "" : buf);
		break;
	case PKG_BOOL:
		b = pkg_object_bool(conf);
		printf("%s\n", b ? "yes" : "no");
		break;
	case PKG_INT:
		integer = pkg_object_int(conf);
		printf("%"PRId64"\n", integer);
		break;
	case PKG_OBJECT:
		while ((o = pkg_object_iterate(conf, &it))) {
			printf("%s: %s\n", pkg_object_key(o), pkg_object_string(o));
		}
		break;
	case PKG_ARRAY:
		while ((o = pkg_object_iterate(conf, &it))) {
			printf("%s\n", pkg_object_string(o));
		}
		break;
	default:
		break;
	}

	return (EX_OK);
}
