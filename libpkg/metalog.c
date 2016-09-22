/*-
 * Copyright (c) 2016 Brad Davis <brd@FreeBSD.org>
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



#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

FILE *metalogfp = NULL;

int
metalog_open(const char *metalog)
{
	metalogfp = fopen(metalog, "a");
	if (metalogfp == NULL) {
		pkg_emit_errno("Unable to open metalog: %s", metalog);
		return EPKG_FATAL;
	} 

	return EPKG_OK;
}

int
metalog_add(int type, const char *path, const char *uname, const char *gname,
    int mode, const char *link)
{
	if (metalogfp == NULL) {
		return EPKG_FATAL;
	}

	// directory
	if (type == 0) {
		fprintf(metalogfp,
		    "./%s type=dir uname=%s gname=%s mode=%3o\n",
		    path, uname, gname, mode);
	}
	// file
	else if (type == 1) {
		fprintf(metalogfp,
		    "./%s type=file uname=%s gname=%s mode=%3o\n",
		    path, uname, gname, mode);
	}
	// link
	else if (type == 2) {
		fprintf(metalogfp,
		    "./%s type=link uname=%s gname=%s mode=%3o link=%s\n",
		    path, uname, gname, mode, link);
	}

	return EPKG_OK;
}

void
metalog_close()
{
	if (metalogfp != NULL) {
		fclose(metalogfp);
	}
}
