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

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sbuf.h>

#include <err.h>
#include <errno.h>
#include <string.h>
#include <sysexits.h>
#include <dirent.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_convert(void)
{
	fprintf(stderr, "usage: pkg convert [-d dir] [-nr]\n\n");
	fprintf(stderr, "For more information see 'pkg help convert'.\n");
}

static int
convert_to_old(const char *pkg_add_dbdir, bool dry_run)
{
	struct pkgdb *db = NULL;
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	struct pkgdb_it *it = NULL;
	char *content, *name, *version, *buf;
	const char *tmp;
	int ret = EX_OK;
	char path[MAXPATHLEN];
	int query_flags = PKG_LOAD_DEPS | PKG_LOAD_FILES |
	    PKG_LOAD_DIRS | PKG_LOAD_SCRIPTS |
	    PKG_LOAD_OPTIONS | PKG_LOAD_MTREE |
	    PKG_LOAD_USERS | PKG_LOAD_GROUPS | PKG_LOAD_RDEPS;
	FILE *fp, *rq;
	struct sbuf *install_script = sbuf_new_auto();
	struct sbuf *deinstall_script = sbuf_new_auto();

	if (mkdir(pkg_add_dbdir, 0755) != 0 && errno != EEXIST)
		err(EX_CANTCREAT, "%s", pkg_add_dbdir);

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, query_flags) == EPKG_OK) {
		rq = NULL;
		pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version);
		printf("Converting %s-%s...", name, version);
		if (dry_run) {
			printf("\n");
			continue;
		}
		pkg_to_old(pkg);
		pkg_old_emit_content(pkg, &content);

		snprintf(path, MAXPATHLEN, "%s/%s-%s", pkg_add_dbdir, name, version);
		mkdir(path, 0755);

		snprintf(path, MAXPATHLEN, "%s/%s-%s/+CONTENTS", pkg_add_dbdir, name, version);
		fp = fopen(path, "w");
		fputs(content, fp);
		fclose(fp);

		pkg_get(pkg, PKG_DESC, &buf);
		snprintf(path, MAXPATHLEN, "%s/%s-%s/+DESC", pkg_add_dbdir, name, version);
		fp = fopen(path, "w");
		fputs(buf, fp);
		fclose(fp);

		pkg_get(pkg, PKG_COMMENT, &buf);
		snprintf(path, MAXPATHLEN, "%s/%s-%s/+COMMENT", pkg_add_dbdir, name, version);
		fp = fopen(path, "w");
		fprintf(fp, "%s\n", buf);
		fclose(fp);

		pkg_get(pkg, PKG_MESSAGE, &buf);
		if (buf != NULL && buf[0] != '\0') {
			snprintf(path, MAXPATHLEN, "%s/%s-%s/+DISPLAY", pkg_add_dbdir, name, version);
			fp = fopen(path, "w");
			fputs(buf, fp);
			fclose(fp);
		}

		pkg_get(pkg, PKG_MTREE, &buf);
		if (buf != NULL && buf[0] != '\0') {
			snprintf(path, MAXPATHLEN, "%s/%s-%s/+MTREE_DIRS", pkg_add_dbdir, name, version);
			fp = fopen(path, "w");
			fputs(buf, fp);
			fclose(fp);
		}

		sbuf_clear(install_script);
		tmp = pkg_script_get(pkg, PKG_SCRIPT_PRE_INSTALL);
		if (tmp != NULL && tmp[0] != '\0') {
			if (sbuf_len(install_script) == 0)
				sbuf_cat(install_script, "#!/bin/sh\n\n");
			sbuf_printf(install_script,
			    "if [ \"$2\" = \"PRE-INSTALL\" ]; then\n"
			    "%s\n"
			    "fi\n",
			    tmp);
		}

		tmp = pkg_script_get(pkg, PKG_SCRIPT_INSTALL);
		if (tmp != NULL && tmp[0] != '\0') {
			if (sbuf_len(install_script) == 0)
				sbuf_cat(install_script, "#!/bin/sh\n\n");
			sbuf_cat(install_script, tmp);
		}

		tmp = pkg_script_get(pkg, PKG_SCRIPT_POST_INSTALL);
		if (tmp != NULL && tmp[0] != '\0') {
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
			snprintf(path, MAXPATHLEN, "%s/%s-%s/+INSTALL", pkg_add_dbdir, name, version);
			fp = fopen(path, "w");
			fputs(sbuf_data(install_script), fp);
			fclose(fp);
		}

		sbuf_clear(deinstall_script);
		tmp = pkg_script_get(pkg, PKG_SCRIPT_PRE_DEINSTALL);
		if (tmp != NULL && tmp[0] != '\0') {
			if (sbuf_len(deinstall_script) == 0)
				sbuf_cat(deinstall_script, "#!/bin/sh\n\n");
			sbuf_printf(deinstall_script,
			    "if [ \"$2\" = \"DEINSTALL\" ]; then\n"
			    "%s\n"
			    "fi\n",
			    tmp);
		}

		tmp = pkg_script_get(pkg, PKG_SCRIPT_DEINSTALL);
		if (tmp != NULL && tmp[0] != '\0') {
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
			snprintf(path, MAXPATHLEN, "%s/%s-%s/+DEINSTALL", pkg_add_dbdir, name, version);
			fp = fopen(path, "w");
			fputs(sbuf_data(deinstall_script), fp);
			fclose(fp);
		}

		snprintf(path, MAXPATHLEN, "%s/%s-%s/+REQUIRED_BY", pkg_add_dbdir, name, version);
		while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
			if (rq == NULL)
				rq = fopen(path, "w");
			fprintf(rq, "%s-%s\n", pkg_dep_name(dep), pkg_dep_version(dep));
		}
		if (rq != NULL)
			fclose(rq);
		printf("done.\n");

		free(content);
	}
	sbuf_delete(install_script);
	sbuf_delete(deinstall_script);

cleanup:
	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (ret);
}

static int
convert_from_old(const char *pkg_add_dbdir, bool dry_run)
{
	DIR *d;
	struct dirent *dp;
	struct pkg *p = NULL;
	char path[MAXPATHLEN];
	struct pkgdb *db = NULL;

	if ((d = opendir(pkg_add_dbdir)) == NULL)
		err(EX_NOINPUT, "%s", pkg_add_dbdir);

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}
	while ((dp = readdir(d)) != NULL) {
		if (dp->d_type == DT_DIR) {
			if (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0)
				continue;
			if (p == NULL)
				pkg_new(&p, PKG_OLD_FILE);
			else
				pkg_reset(p, PKG_OLD_FILE);
			printf("Converting %s...\n", dp->d_name);
			snprintf(path, MAXPATHLEN, "%s/%s", pkg_add_dbdir, dp->d_name);
			if (pkg_old_load_from_path(p, path) != EPKG_OK) {
				fprintf(stderr, "Skipping invalid package: %s\n", path);
				continue;
			}
			pkg_from_old(p);
			if (!dry_run)
				pkgdb_register_ports(db, p);
		}
	}

	pkg_free(p);
	pkgdb_close(db);
	return (EX_OK);
}

int
exec_convert(int argc, char **argv)
{
	int ch;
	bool revert = false;
	bool dry_run = false;
	const char *pkg_add_dbdir = "/var/db/pkg";

	while ((ch = getopt(argc, argv, "d:nr")) != -1) {
		switch (ch) {
		case 'd':
			pkg_add_dbdir = optarg;
			break;
		case 'n':
			dry_run = true;
			break;
		case 'r':
			revert = true;
			break;
		default:
			usage_convert();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1) {
		usage_convert();
		return (EX_USAGE);
	}

	printf("Converting packages %s %s\n", revert ? "to" : "from", pkg_add_dbdir);

	if (revert)
		return (convert_to_old(pkg_add_dbdir, dry_run));
	else
		return (convert_from_old(pkg_add_dbdir, dry_run));
}
