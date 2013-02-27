/*
 * Copyright (c) 2013 Sofian Brabez <sbz@FreeBSD.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pkg.h>

#include "pkgcli.h"

#define TERM_COUNT 7

struct {
	const char *name;
	const char *escape_begin; /* hexadecimal or octal value see ascii(7) */
	const char *escape_end; /* hexadecimal or octal value see ascii(7) */
} terms[TERM_COUNT] = {
	{"xterm", 	"\033]0;", 	"\007"},
	{"eterm", 	"\033]0;", 	"\007"},
	{"aterm",	"\033]0;", 	"\007"},
	{"kterm", 	"\033]0;", 	"\007"},
	{"rxvt",  	"\033]0;", 	"\007"},
	{"screen",	"\x1bk", 	"\x1b\\"},
	{"tmux", 	"\x1bk", 	"\x1b\\"}
};

void
pkg_title(struct pkg *pkg, const char *message) {
	int i;
	char *term = getenv("TERM");
	char *pkgname, *pkgversion;

	pkg_get(pkg, PKG_NAME, &pkgname);
	pkg_get(pkg, PKG_VERSION, &pkgversion);

	for (i=0; i<TERM_COUNT-1; i++) {
		if (!strcasecmp(terms[i].name, term) && isatty(fileno(stdout))) {
			fprintf(stdout, "%s[%d/%d] %s %s-%s%s",
					terms[i].escape_begin,
					nbdone,
					nbactions,
					message,
					pkgname,
					pkgversion,
					terms[i].escape_end
			);
			break;
		}
	}
}
