/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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
#include <libgen.h>
#include <string.h>
#include <errno.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

#include <bsd_compat.h>

/* Number of pages to copy per call to sqlite3_backup_step()
   Default page size is 1024 bytes on Unix */
#define NPAGES	4

static int
ps_cb(void *ps, int ncols, char **coltext, __unused char **colnames)
{
	/* We should have exactly one row and one column of output */
	if (ncols != 1)
		return (-1);	/* ABORT! */

	*(off_t *)ps = strtoll(coltext[0], NULL, 10);

	return (0);
}

static int
copy_database(sqlite3 *src, sqlite3 *dst, const char *name)
{
	sqlite3_backup	*b;
	char		*errmsg;
	off_t		 total;
	off_t		 done;
	off_t		 page_size;
	int		 ret;

	assert(src != NULL);
	assert(dst != NULL);

	ret = sqlite3_exec(dst, "PRAGMA main.locking_mode=EXCLUSIVE;"
			   "BEGIN IMMEDIATE;COMMIT;", NULL, NULL, &errmsg);
	if (ret != SQLITE_OK) {
		pkg_emit_error("sqlite error -- %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}

	ret = sqlite3_exec(dst, "PRAGMA page_size", ps_cb, &page_size, &errmsg);
	if (ret != SQLITE_OK) {
		pkg_emit_error("sqlite error -- %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}

	b = sqlite3_backup_init(dst, "main", src, "main");

	done = total = 0;

	pkg_emit_progress_start(NULL);
	do {
		ret = sqlite3_backup_step(b, NPAGES);
		total = sqlite3_backup_pagecount(b);
		done = total - sqlite3_backup_remaining(b);
		pkg_emit_progress_tick(done, total);

		if (ret != SQLITE_OK && ret != SQLITE_DONE ) {
			if (ret == SQLITE_BUSY) {
				sqlite3_sleep(250);
			} else {
				ERROR_SQLITE(dst, "backup step");
				break;
			}
		}
	} while(done < total);

	ret = sqlite3_backup_finish(b);
	pkg_emit_progress_tick(total, total);

	sqlite3_exec(dst, "PRAGMA main.locking_mode=NORMAL;"
			   "BEGIN IMMEDIATE;COMMIT;", NULL, NULL, &errmsg);

	if (ret != SQLITE_OK) {
		pkg_emit_error("sqlite error -- %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}

	return ret;
}

int
pkgdb_dump(struct pkgdb *db, const char *dest)
{
	sqlite3	*backup;
	int	 ret;

	if (eaccess(dest, W_OK)) {
		if (errno != ENOENT) {
			pkg_emit_error("eaccess(%s) -- %s", dest,
			    strerror(errno));
			return (EPKG_FATAL);
		}

		/* Could we create the Sqlite DB file? */
		if (eaccess(bsd_dirname(dest), W_OK)) {
			pkg_emit_error("eaccess(%s) -- %s", bsd_dirname(dest),
			    strerror(errno));
			return (EPKG_FATAL);
		}
	}

	ret = sqlite3_open(dest, &backup);

	if (ret != SQLITE_OK) {
		ERROR_SQLITE(backup, "sqlite3_open");
		sqlite3_close(backup);
		return (EPKG_FATAL);
	}

	pkg_emit_backup();
	ret = copy_database(db->sqlite, backup, dest);

	sqlite3_close(backup);

	return (ret == SQLITE_OK? EPKG_OK : EPKG_FATAL);
}

int
pkgdb_load(struct pkgdb *db, const char *src)
{
	sqlite3	*restore;
	int	 ret;

	if (eaccess(src, R_OK)) {
		pkg_emit_error("eaccess(%s) -- %s", src, strerror(errno));
		return (EPKG_FATAL);
	}

	ret = sqlite3_open(src, &restore);

	if (ret != SQLITE_OK) {
		ERROR_SQLITE(restore, "sqlite3_open");
		sqlite3_close(restore);
		return (EPKG_FATAL);
	}

	pkg_emit_restore();
	ret = copy_database(restore, db->sqlite, src);

	sqlite3_close(restore);

	return (ret == SQLITE_OK? EPKG_OK : EPKG_FATAL);
}
