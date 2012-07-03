/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
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

#include "pkg.h"
#include "private/pkg.h"

int
pkg_script_run(struct pkg * const pkg, pkg_script type)
{
	struct sbuf * const script_cmd = sbuf_new_auto();
	size_t i;
	const char *name, *prefix, *version;

	struct {
		const char * const arg;
		const pkg_script b;
		const pkg_script a;
	} const map[] = {
		/* a implies b with argument arg */
		{"PRE-INSTALL",    PKG_SCRIPT_INSTALL,   PKG_SCRIPT_PRE_INSTALL},
		{"POST-INSTALL",   PKG_SCRIPT_INSTALL,   PKG_SCRIPT_POST_INSTALL},
		{"PRE-UPGRADE",    PKG_SCRIPT_UPGRADE,   PKG_SCRIPT_PRE_UPGRADE},
		{"POST-UPGRADE",   PKG_SCRIPT_UPGRADE,   PKG_SCRIPT_POST_UPGRADE},
		{"DEINSTALL",      PKG_SCRIPT_DEINSTALL, PKG_SCRIPT_PRE_DEINSTALL},
		{"POST-DEINSTALL", PKG_SCRIPT_DEINSTALL, PKG_SCRIPT_POST_DEINSTALL},
	};

	pkg_get(pkg, PKG_PREFIX, &prefix, PKG_NAME, &name, PKG_VERSION, &version);

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (map[i].a == type)
			break;
	}

	assert(i < sizeof(map) / sizeof(map[0]));
	assert(map[i].a == type);

	for (i = 0; i < PKG_NUM_SCRIPTS; i++) {
		if (pkg_script_get(pkg, i) == NULL)
			continue;
		if (i == map[i].a || i == map[i].b) {
			sbuf_reset(script_cmd);
			sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s",
			    prefix, name, version);

			if (i == map[i].b) {
				/* add arg **/
				sbuf_cat(script_cmd, " ");
				sbuf_cat(script_cmd, map[i].arg);
			}

			sbuf_cat(script_cmd, "\n");
			sbuf_cat(script_cmd, pkg_script_get(pkg, i));
			sbuf_finish(script_cmd);
			system(sbuf_get(script_cmd));
		}
	}

	sbuf_delete(script_cmd);

	return (EPKG_OK);
}

