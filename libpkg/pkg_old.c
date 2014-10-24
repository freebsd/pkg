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
pkg_old_emit_content(struct pkg *pkg, char **dest)
{
	struct sbuf *content = sbuf_new_auto();

	struct pkg_dep *dep = NULL;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	struct pkg_option *option = NULL;

	char option_type = 0;

	pkg_sbuf_printf(content,
	    "@comment PKG_FORMAT_REVISION:1.1\n"
	    "@name %n-%v\n"
	    "@comment ORIGIN:%o\n"
	    "@cwd %p\n"
	    /* hack because we can recreate the prefix split or origin */
	    "@cwd /\n", pkg, pkg, pkg, pkg);

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		sbuf_printf(content,
		    "@pkgdep %s-%s\n"
		    "@comment DEPORIGIN:%s\n",
		    pkg_dep_name(dep),
		    pkg_dep_version(dep),
		    pkg_dep_origin(dep));
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		sbuf_printf(content,
		    "%s\n"
		    "@comment MD5:%s\n",
		     file->path + 1,
		     file->sum);
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		sbuf_printf(content,
		    "@unexec /sbin/rmdir \"%s\" 2>/dev/null\n",
		    dir->path);
	}

	sbuf_printf(content, "@comment OPTIONS:");
	while (pkg_options(pkg, &option) == EPKG_OK) {
		/* Add space for previous option, if not the first. */
		if (option_type != 0)
			sbuf_cat(content, " ");

		if (strcmp(pkg_option_value(option), "on") == 0)
			option_type = '+';
		else
			option_type = '-';
		sbuf_printf(content, "%c%s",
		    option_type,
		    pkg_option_opt(option));
	}
	sbuf_printf(content, "\n");

	sbuf_finish(content);
	*dest = strdup(sbuf_get(content));
	sbuf_delete(content);

	return (EPKG_OK);
}

int
pkg_to_old(struct pkg *p)
{
	struct pkg_file *f = NULL;
	char md5[MD5_DIGEST_LENGTH * 2 + 1];

	p->type = PKG_OLD_FILE;
	while (pkg_files(p, &f) == EPKG_OK) {
		if (f->sum[0] == '\0')
			continue;
		if (md5_file(f->path, md5) == EPKG_OK)
			strlcpy(f->sum, md5, sizeof(f->sum));
	}

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

int
pkg_register_old(struct pkg *pkg)
{
	FILE *fp;
	char *content;
	const char *pkgdbdir, *tmp;
	char path[MAXPATHLEN];
	struct sbuf *install_script = sbuf_new_auto();
	struct sbuf *deinstall_script = sbuf_new_auto();
	struct pkg_dep *dep = NULL;

	pkg_to_old(pkg);
	pkg_old_emit_content(pkg, &content);

	pkgdbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	pkg_snprintf(path, sizeof(path), "%S/%n-%v", pkgdbdir, pkg, pkg);
	mkdir(path, 0755);

	pkg_snprintf(path, sizeof(path), "%S/%n-%v/+CONTENTS", pkgdbdir, pkg, pkg);
	fp = fopen(path, "w");
	fputs(content, fp);
	fclose(fp);

	pkg_snprintf(path, sizeof(path), "%S/%n-%v/+DESC", pkgdbdir, pkg, pkg);
	fp = fopen(path, "w");
	pkg_fprintf(fp, "%e", pkg);
	fclose(fp);

	pkg_snprintf(path, sizeof(path), "%s/%n-%v/+COMMENT", pkgdbdir, pkg, pkg);
	fp = fopen(path, "w");
	pkg_fprintf(fp, "%c\n", pkg);
	fclose(fp);

	if (pkg_has_message(pkg)) {
		pkg_snprintf(path, sizeof(path), "%s/%n-%v/+DISPLAY", pkgdbdir, pkg, pkg);
		fp = fopen(path, "w");
		pkg_fprintf(fp, "%M", pkg);
		fclose(fp);
	}

	sbuf_clear(install_script);
	tmp = pkg_script_get(pkg, PKG_SCRIPT_PRE_INSTALL);
	if (tmp != NULL && *tmp != '\0') {
		if (sbuf_len(install_script) == 0)
			sbuf_cat(install_script, "#!/bin/sh\n\n");
		sbuf_printf(install_script,
		    "if [ \"$2\" = \"PRE-INSTALL\" ]; then\n"
		    "%s\n"
		    "fi\n",
		    tmp);
	}

	tmp = pkg_script_get(pkg, PKG_SCRIPT_INSTALL);
	if (tmp != NULL && *tmp != '\0') {
		if (sbuf_len(install_script) == 0)
			sbuf_cat(install_script, "#!/bin/sh\n\n");
		sbuf_cat(install_script, tmp);
	}

	tmp = pkg_script_get(pkg, PKG_SCRIPT_POST_INSTALL);
	if (tmp != NULL && *tmp != '\0') {
		if (sbuf_len(install_script) == 0)
			sbuf_cat(install_script, "#!/bin/sh\n\n");
		sbuf_printf(install_script,
		    "if [ \"$2\" = \"POST-INSTALL\" ]; then\n"
		    "%s\n"
		    "fi\n",
		    tmp);
	}
	if (sbuf_len(install_script) > 0) {
		sbuf_finish(install_script);
		pkg_snprintf(path, sizeof(path), "%s/%n-%v/+INSTALL", pkgdbdir, pkg, pkg);
		fp = fopen(path, "w");
		fputs(sbuf_data(install_script), fp);
		fclose(fp);
	}

	sbuf_clear(deinstall_script);
	tmp = pkg_script_get(pkg, PKG_SCRIPT_PRE_DEINSTALL);
	if (tmp != NULL && *tmp != '\0') {
		if (sbuf_len(deinstall_script) == 0)
			sbuf_cat(deinstall_script, "#!/bin/sh\n\n");
		sbuf_printf(deinstall_script,
		    "if [ \"$2\" = \"DEINSTALL\" ]; then\n"
		    "%s\n"
		    "fi\n",
		    tmp);
	}

	tmp = pkg_script_get(pkg, PKG_SCRIPT_DEINSTALL);
	if (tmp != NULL && *tmp != '\0') {
		if (sbuf_len(deinstall_script) == 0)
			sbuf_cat(deinstall_script, "#!/bin/sh\n\n");
		sbuf_cat(deinstall_script, tmp);
	}

	tmp = pkg_script_get(pkg, PKG_SCRIPT_POST_DEINSTALL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (sbuf_len(deinstall_script) == 0)
			sbuf_cat(deinstall_script, "#!/bin/sh\n\n");
		sbuf_printf(deinstall_script,
		    "if [ \"$2\" = \"POST-DEINSTALL\" ]; then\n"
		    "%s\n"
		    "fi\n",
		    tmp);
	}
	if (sbuf_len(deinstall_script) > 0) {
		sbuf_finish(deinstall_script);
		pkg_snprintf(path, sizeof(path), "%s/%n-%v/+DEINSTALL", pkgdbdir, pkg, pkg);
		fp = fopen(path, "w");
		fputs(sbuf_data(deinstall_script), fp);
		fclose(fp);
	}

	while (pkg_deps(pkg, &dep)) {
		snprintf(path, sizeof(path), "%s/%s-%s/+REQUIRED_BY", pkgdbdir,
		    pkg_dep_name(dep), pkg_dep_version(dep));
		fp = fopen(path, "a");
		pkg_fprintf(fp, "%n-%v\n", pkg, pkg);
		fclose(fp);
	}

	return (EPKG_OK);
}
