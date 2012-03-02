/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <archive_entry.h>
#include <string.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

int
pkgdb_dump(struct pkgdb *db, char *dest)
{
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	struct sbuf *path = NULL;
	struct packing *pack = NULL;
	char *m = NULL;
	int ret = EPKG_OK;
	int query_flags = PKG_LOAD_DEPS | PKG_LOAD_FILES | PKG_LOAD_CATEGORIES |
	    PKG_LOAD_DIRS | PKG_LOAD_SCRIPTS | PKG_LOAD_OPTIONS |
	    PKG_LOAD_MTREE | PKG_LOAD_LICENSES;

	packing_init(&pack, dest ? dest : "./pkgdump", TXZ);

	path = sbuf_new_auto();
	if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL) {
		/* TODO handle errors */
		return (EPKG_FATAL);
	}

	while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
		const char *name, *version, *mtree;

		pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version, PKG_MTREE, &mtree);
		pkg_emit_manifest(pkg, &m);
		sbuf_clear(path);
		sbuf_printf(path, "%s-%s.yaml", name, version);
		sbuf_finish(path);
		packing_append_buffer(pack, m, sbuf_get(path), strlen(m));
		free(m);
		if (mtree != NULL) {
			sbuf_clear(path);
			sbuf_printf(path, "%s-%s.mtree", name, version);
			sbuf_finish(path);
			packing_append_buffer(pack, mtree, sbuf_get(path), strlen(mtree));
		}
	}

	sbuf_delete(path);
	packing_finish(pack);
	return (EPKG_OK);
}

int
pkgdb_load(struct pkgdb *db, char *dest)
{
	struct pkg *pkg = NULL;
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;
	const char *path = NULL;
	size_t len = 0;
	char *buf = NULL;
	size_t size = 0;
	int retcode = EPKG_OK;

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	if (archive_read_open_filename(a, dest, 4096) != ARCHIVE_OK) {
		pkg_emit_error("archive_read_open_filename(%s): %s", dest,
					   archive_error_string(a));
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		path = archive_entry_pathname(ae);
		len = strlen(path);
		if (len < 6)
			continue;
		if (!strcmp(path + len - 5, ".yaml")) {
			if (pkg == NULL) {
				pkg_new(&pkg, PKG_FILE);
			} else {
				pkgdb_register_finale(db, pkgdb_register_pkg(db, pkg, 0));
				pkg_reset(pkg, PKG_FILE);
			}
			size = archive_entry_size(ae);
			buf = calloc(1, size + 1);
			archive_read_data(a, buf, size);
			pkg_parse_manifest(pkg, buf);
			free(buf);
		} else if (!strcmp(path + len - 6, ".mtree")) {
			size = archive_entry_size(ae);
			buf = calloc(1, size + 1);
			archive_read_data(a, buf, size);
			pkg_set(pkg, PKG_MTREE, buf);
			free(buf);
		} else 
			continue;
	}
	if (pkg != NULL)
		pkgdb_register_pkg(db, pkg, 1);

cleanup:
	if (a != NULL)
		archive_read_finish(a);
	pkgdb_close(db);
	pkg_free(pkg);

	return (retcode);
}
