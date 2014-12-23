/*-
 * Copyright (c) 2012-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2013 Bryan Drewery <bdrewery@FreeBSD.org>
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

#include <regex.h>

#include <pkg.h>
#include <private/pkg.h>

static const char * const scripts[] = {
	"+INSTALL",
	"+PRE_INSTALL",
	"+POST_INSTALL",
	"+POST_INSTALL",
	"+DEINSTALL",
	"+PRE_DEINSTALL",
	"+POST_DEINSTALL",
	"+UPGRADE",
	"+PRE_UPGRADE",
	"+POST_UPGRADE",
	"pkg-install",
	"pkg-pre-install",
	"pkg-post-install",
	"pkg-deinstall",
	"pkg-pre-deinstall",
	"pkg-post-deinstall",
	"pkg-upgrade",
	"pkg-pre-upgrade",
	"pkg-post-upgrade",
	NULL
};

int
pkg_old_load_from_path(struct pkg *pkg, const char *path)
{
	char fpath[MAXPATHLEN];
	regex_t preg;
	regmatch_t pmatch[2];
	int i;
	size_t size;
	char myarch[BUFSIZ];

	if (!is_dir(path))
		return (EPKG_FATAL);

	snprintf(fpath, sizeof(fpath), "%s/+CONTENTS", path);
	if (ports_parse_plist(pkg, fpath, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	snprintf(fpath, sizeof(fpath), "%s/+COMMENT", path);
	if (access(fpath, F_OK) == 0)
		pkg_set_from_file(pkg, PKG_COMMENT, fpath, true);

	snprintf(fpath, sizeof(fpath), "%s/+DESC", path);
	if (access(fpath, F_OK) == 0)
		pkg_set_from_file(pkg, PKG_DESC, fpath, false);

	snprintf(fpath, sizeof(fpath), "%s/+DISPLAY", path);
	if (access(fpath, F_OK) == 0)
		pkg_set_from_file(pkg, PKG_MESSAGE, fpath, false);

	snprintf(fpath, sizeof(fpath), "%s/+MTREE_DIRS", path);
	if (access(fpath, F_OK) == 0)
		pkg_set_from_file(pkg, PKG_MTREE, fpath, false);

	for (i = 0; scripts[i] != NULL; i++) {
		snprintf(fpath, sizeof(fpath), "%s/%s", path, scripts[i]);
		if (access(fpath, F_OK) == 0)
			pkg_addscript_file(pkg, fpath);
	}

	pkg_get_myarch(myarch, BUFSIZ);
	pkg->arch = strdup(myarch);
	pkg->maintainer = strdup("unknown");
	regcomp(&preg, "^WWW:[[:space:]]*(.*)$", REG_EXTENDED|REG_ICASE|REG_NEWLINE);
	if (regexec(&preg, pkg->desc, 2, pmatch, 0) == 0) {
		size = pmatch[1].rm_eo - pmatch[1].rm_so;
		pkg->www = strndup(&pkg->desc[pmatch[1].rm_so], size);
	} else {
		pkg->www = strdup("UNKNOWN");
	}
	regfree(&preg);

	return (EPKG_OK);
}

int
pkg_from_old(struct pkg *p)
{
	struct pkg_file *f = NULL;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];

	p->type = PKG_INSTALLED;
	while (pkg_files(p, &f) == EPKG_OK) {
		if (f->sum[0] == '\0')
			continue;
		if (sha256_file(f->path, sha256) == EPKG_OK)
			strlcpy(f->sum, sha256, sizeof(f->sum));
	}

	return (EPKG_OK);
}
