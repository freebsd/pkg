/*-
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/stat.h>
#include <sys/queue.h>
/* For MIN */
#include <sys/param.h>

#include <assert.h>
#include <err.h>
#include <fts.h>
#include <getopt.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <pkg.h>
#include <stdbool.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <uthash.h>

#include <bsd_compat.h>

#include "pkgcli.h"

struct deletion_list {
	STAILQ_ENTRY(deletion_list) next;
	char	*path;
};

struct sumlist {
	char sum[PKG_FILE_CKSUM_CHARS + 1];
	UT_hash_handle hh;
};

#define OUT_OF_DATE	(1U<<0)
#define REMOVED		(1U<<1)
#define CKSUM_MISMATCH	(1U<<2)
#define SIZE_MISMATCH	(1U<<3)
#define ALL		(1U<<4)

STAILQ_HEAD(dl_head, deletion_list);

static int
add_to_dellist(struct dl_head *dl,  const char *path)
{
	struct deletion_list	*dl_entry;
	static bool first_entry = true;

	assert(path != NULL);

	dl_entry = calloc(1, sizeof(struct deletion_list));
	if (dl_entry == NULL) {
		warn("adding deletion list entry");
		return (EPKG_FATAL);
	}

	dl_entry->path = strdup(path);

	if (!quiet) {
		if (first_entry) {
			first_entry = false;
			printf("The following package files will be deleted:"
			    "\n");
		}
		printf("\t%s\n", dl_entry->path);
	}

	STAILQ_INSERT_TAIL(dl, dl_entry, next);

	return (EPKG_OK);
}

static void
free_dellist(struct dl_head *dl)
{
	struct deletion_list	*dl_entry;

	while (!STAILQ_EMPTY(dl)) {
		dl_entry = STAILQ_FIRST(dl);
		STAILQ_REMOVE_HEAD(dl, next);
		free(dl_entry->path);
	}
}

static int
delete_dellist(struct dl_head *dl, int total)
{
	struct deletion_list	*dl_entry;
	int			retcode = EX_OK;
	int			count = 0, processed = 0;

	progressbar_start("Deleting files");
	STAILQ_FOREACH(dl_entry, dl, next) {
		if (unlink(dl_entry->path) != 0) {
			warn("unlink(%s)", dl_entry->path);
			count++;
			retcode = EX_SOFTWARE;
		}
		++processed;
		progressbar_tick(processed, total);
	}
	progressbar_tick(processed, total);

	if (!quiet) {
		if (retcode == EX_OK)
			printf("All done\n");
		else
			printf("%d package%s could not be deleted\n",
			       count, count > 1 ? "s" : "");
	}
	return (retcode);
}

/*
 * Extract hash from filename in format <name>-<version>-<hash>.txz
 */
static bool
extract_filename_sum(const char *fname, char sum[])
{
	const char *dash_pos, *dot_pos;

	dot_pos = strrchr(fname, '.');
	if (dot_pos == NULL)
		dot_pos = fname + strlen(fname);

	dash_pos = strrchr(fname, '-');
	if (dash_pos == NULL)
		return (false);
	else if (dot_pos < dash_pos)
		dot_pos = fname + strlen(fname);

	if (dot_pos - dash_pos != PKG_FILE_CKSUM_CHARS + 1)
		return (false);

	strlcpy(sum, dash_pos + 1, PKG_FILE_CKSUM_CHARS + 1);
	return (true);
}

void
usage_clean(void)
{
	fprintf(stderr, "Usage: pkg clean [-anqy]\n\n");
	fprintf(stderr, "For more information see 'pkg help clean'.\n");
}

int
exec_clean(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	struct pkgdb_it	*it = NULL;
	struct pkg	*p = NULL;
	struct sumlist	*sumlist = NULL, *s, *t;
	FTS		*fts = NULL;
	FTSENT		*ent = NULL;
	struct dl_head	dl = STAILQ_HEAD_INITIALIZER(dl);
	const char	*cachedir, *sum, *name;
	char		*paths[2], csum[PKG_FILE_CKSUM_CHARS + 1],
			link_buf[MAXPATHLEN];
	bool		 all = false;
	bool		 sumloaded = false;
	int		 retcode;
	int		 ch, cnt = 0;
	size_t		 total = 0, slen;
	ssize_t		 link_len;
	char		 size[7];
	struct pkg_manifest_key *keys = NULL;

	struct option longopts[] = {
		{ "all",	no_argument,	NULL,	'a' },
		{ "dry-run",	no_argument,	NULL,	'n' },
		{ "quiet",	no_argument,	NULL,	'q' },
		{ "yes",	no_argument,	NULL,	'y' },
		{ NULL,		0,		NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+anqy", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			all = true;
			break;
		case 'n':
			dry_run = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'y':
			yes = true;
			break;
		default:
			usage_clean();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));

	paths[0] = __DECONST(char*, cachedir);
	paths[1] = NULL;

	retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_REPO);

	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to clean old packages");
		return (EX_NOPERM);
	} else if (retcode == EPKG_ENODB) {
		warnx("No package database installed.  Nothing to do!");
		return (EX_OK);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EX_SOFTWARE);
	}

	retcode = EX_SOFTWARE;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if ((fts = fts_open(paths, FTS_PHYSICAL, NULL)) == NULL) {
		warn("fts_open(%s)", cachedir);
		goto cleanup;
	}

	/* Build the list of out-of-date or obsolete packages */

	pkg_manifest_keys_new(&keys);
	while ((ent = fts_read(fts)) != NULL) {
		if (ent->fts_info != FTS_F && ent->fts_info != FTS_SL)
			continue;

		if (all) {
			retcode = add_to_dellist(&dl, ent->fts_path);
			if (retcode == EPKG_OK) {
				total += ent->fts_statp->st_size;
				++cnt;
			}
			continue;
		}

		if (sumlist == NULL && !sumloaded) {
			it = pkgdb_repo_search(db, "*", MATCH_GLOB, FIELD_NAME, FIELD_NONE, NULL);
			while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
				pkg_get(p, PKG_CKSUM, &sum);
				slen = MIN(strlen(sum), PKG_FILE_CKSUM_CHARS);
				s = calloc(1, sizeof(struct sumlist));
				memcpy(s->sum, sum, slen);
				s->sum[slen] = '\0';
				HASH_ADD_STR(sumlist, sum, s);
			}
		}

		if (ent->fts_info == FTS_SL) {
			/* Dereference the symlink and check it for being
			 * recognized checksum file, or delete the symlink
			 * later. */
			if ((link_len = readlink(ent->fts_name, link_buf,
			    sizeof(link_buf))) == -1)
				continue;
			link_buf[link_len] = '\0';
			name = link_buf;
		} else
			name = ent->fts_name;

		s = NULL;
		if (extract_filename_sum(name, csum))
			HASH_FIND_STR(sumlist, csum, s);
		if (s == NULL) {
			retcode = add_to_dellist(&dl, ent->fts_path);
			if (retcode == EPKG_OK) {
				total += ent->fts_statp->st_size;
				++cnt;
			}
			continue;
		}
	}
	HASH_ITER(hh, sumlist, s, t) {
		HASH_DEL(sumlist, s);
		free(s);
	}

	if (STAILQ_EMPTY(&dl)) {
		if (!quiet)
			printf("Nothing to do.\n");
		retcode = EX_OK;
		goto cleanup;
	}

	humanize_number(size, sizeof(size), total, "B", HN_AUTOSCALE, 0);

	printf("The cleanup will free %s\n", size);
	if (!dry_run) {
			if (query_yesno(false,
			  "\nProceed with cleaning the cache? [y/N]: ")) {
				retcode = delete_dellist(&dl, cnt);
			}
	} else {
		retcode = EX_OK;
	}

cleanup:
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);
	pkg_manifest_keys_free(keys);
	free_dellist(&dl);

	if (fts != NULL)
		fts_close(fts);

	return (retcode);
}
