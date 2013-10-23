/*-
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
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

#include <sys/stat.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <fts.h>
#include <pkg.h>
#include <stdbool.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pkgcli.h"

struct deletion_list {
	STAILQ_ENTRY(deletion_list) next;
	unsigned	 reason;
	const char	*path;
	const char	*origin;
	const char	*newname;
	const char	*newversion;
	char		 data[0];
};
#define OUT_OF_DATE	(1U<<0)
#define REMOVED		(1U<<1)
#define CKSUM_MISMATCH	(1U<<2)
#define SIZE_MISMATCH	(1U<<3)
#define ALL		(1U<<4)

STAILQ_HEAD(dl_head, deletion_list);

static int
add_to_dellist(struct dl_head *dl,  unsigned reason, const char *path,
	       const char *origin, const char *newname, const char *newversion)
{
	struct deletion_list	*dl_entry;
	size_t			 alloc_len;
	size_t			 offset;

	assert(path != NULL);
	assert(origin != NULL);

	alloc_len = sizeof(struct deletion_list) + strlen(path) +
		strlen(origin) + 2;
	if (newname != NULL)
		alloc_len += strlen(newname) + 1;
	if (newversion != NULL)
		alloc_len += strlen(newversion) + 1;

	dl_entry = calloc(1, alloc_len);
	if (dl_entry == NULL) {
		warn("adding deletion list entry");
		return (EPKG_FATAL);
	}

	dl_entry->reason = reason;

	offset = 0;

	alloc_len = strlen(path) + 1;
	strlcpy(&(dl_entry->data[offset]), path, alloc_len);
	dl_entry->path = &(dl_entry->data[offset]);
	offset = alloc_len;

	alloc_len = strlen(origin) + 1;
	strlcpy(&(dl_entry->data[offset]), origin, alloc_len);
	dl_entry->origin = &(dl_entry->data[offset]);
	offset += alloc_len;

	if (newname != NULL) {
		alloc_len = strlen(newname) + 1;
		strlcpy(&(dl_entry->data[offset]), newname, alloc_len);
		dl_entry->newname = &(dl_entry->data[offset]);
		offset += alloc_len;
	} else
		dl_entry->newname = NULL;

	if (newversion != NULL) {
		alloc_len = strlen(newversion) + 1;
		strlcpy(&(dl_entry->data[offset]), newversion, alloc_len);
		dl_entry->newversion = &(dl_entry->data[offset]);
		offset += alloc_len;
	} else
		dl_entry->newversion = NULL;

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
		free(dl_entry);
	}
}

static void
display_dellist(struct dl_head *dl, const char *cachedir)
{
	struct deletion_list	*dl_entry;
	const char		*relpath;

	printf("The following package files will be deleted "
	       "from the cache directory\n%s:\n\n", cachedir);

	printf("%-30s %-20s %s\n", "Package:", "Origin:", "Reason:");
	STAILQ_FOREACH(dl_entry, dl, next) {
		if (strlen(cachedir) + 1 < strlen(dl_entry->path)) {
			relpath = dl_entry->path + strlen(cachedir);
			if (relpath[0] == '/')
				relpath++;
		} else
			relpath = dl_entry->path;

		printf("%-30s %-20s ", relpath, dl_entry->origin);

		switch (dl_entry->reason) {
		case OUT_OF_DATE:
			printf("Superseded by %s-%s\n",
			    dl_entry->newname != NULL ?
			       dl_entry->newname :
			       "(unknown)",
			    dl_entry->newversion != NULL ?
			       dl_entry->newversion :
			       "(unknown)");
			break;
		case REMOVED:
			printf("Removed from the repository\n");
			break;
		case CKSUM_MISMATCH:
			printf("Checksum mismatch\n");
			break;
		case ALL:
			printf("Removing all\n");
			break;
		case SIZE_MISMATCH:
			printf("Size mismatch\n");
			break;
		default:	/* not reached */
			break;
		}
	}
}

static int
delete_dellist(struct dl_head *dl)
{
	struct deletion_list	*dl_entry;
	int			retcode = EX_OK;
	int			count = 0;

	if (!quiet)
		printf("Deleting:\n");

	STAILQ_FOREACH(dl_entry, dl, next) {
		if (!quiet)
			printf("\t%s\n", dl_entry->path);
		if (unlink(dl_entry->path) != 0) {
			warn("unlink(%s)", dl_entry->path);
			count++;
			retcode = EX_SOFTWARE;
		}
	}

	if (!quiet) {
		if (retcode == EX_OK)
			printf("All done\n");
		else
			printf("%d package%s could not be deleted\n",
			       count, count > 1 ? "s" : "");
	}
	return (retcode);
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
	struct pkg	*pkg = NULL;
	struct pkg	*p = NULL;
	FTS		*fts = NULL;
	FTSENT		*ent = NULL;
	struct dl_head	dl = STAILQ_HEAD_INITIALIZER(dl);
	const char	*cachedir;
	char		*paths[2];
	char		*repopath;
	bool		 all = false;
	bool		 dry_run = false;
	bool		 yes;
	int		 retcode;
	int		 ret;
	int		 ch;
	struct pkg_manifest_key *keys = NULL;

	pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);

	while ((ch = getopt(argc, argv, "anqy")) != -1) {
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

	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK) {
		warnx("Cannot get cachedir config entry");
		return 1;
	}

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

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		goto cleanup;
	}

	if ((fts = fts_open(paths, FTS_PHYSICAL, NULL)) == NULL) {
		warn("fts_open(%s)", cachedir);
		goto cleanup;
	}

	/* Build the list of out-of-date or obsolete packages */

	pkg_manifest_keys_new(&keys);
	while ((ent = fts_read(fts)) != NULL) {
		const char *origin, *pkgrepopath;

		if (ent->fts_info != FTS_F)
			continue;

		repopath = ent->fts_path + strlen(cachedir);
		if (repopath[0] == '/')
			repopath++;

		if (pkg_open(&pkg, ent->fts_path, keys, PKG_OPEN_MANIFEST_ONLY) != EPKG_OK) {
			if (!quiet)
				warnx("skipping %s", ent->fts_path);
			continue;
		}

		pkg_get(pkg, PKG_ORIGIN, &origin);
		it = pkgdb_search(db, origin, MATCH_EXACT, FIELD_ORIGIN,
		    FIELD_NONE, NULL);

		if (it == NULL) {
			if (!quiet)
				warnx("skipping %s", ent->fts_path);
			continue;
		}

		if (all) {
			ret = add_to_dellist(&dl, ALL, ent->fts_path,
					     origin, NULL, NULL);
			pkgdb_it_free(it);
			continue;
		}

		ret = pkgdb_it_next(it, &p, PKG_LOAD_BASIC);
		if (ret == EPKG_FATAL) {
			if (!quiet)
				warnx("skipping %s", ent->fts_path);
			continue;
		}

		if (ret == EPKG_END) {
			/* No matching package found in repo */
			ret = add_to_dellist(&dl, REMOVED, ent->fts_path,
					     origin, NULL, NULL);
			continue;
		}

		pkg_get(p, PKG_REPOPATH, &pkgrepopath);

		if (strcmp(repopath, pkgrepopath)) {
			const char	*newname;
			const char	*newversion;

			pkg_get(p,
				PKG_NAME,    &newname,
				PKG_VERSION, &newversion);

			ret = add_to_dellist(&dl, OUT_OF_DATE, ent->fts_path,
					     origin, newname, newversion);
		} else {
			char local_cksum[SHA256_DIGEST_LENGTH * 2 + 1];
			const char *cksum;
			int64_t size;

			pkg_get(p, PKG_CKSUM, &cksum, PKG_PKGSIZE, &size);

			if (ent->fts_statp->st_size != size) {
				ret = add_to_dellist(&dl, SIZE_MISMATCH,
				                  ent->fts_path, origin, NULL, NULL);
			} else if (hash_file(ent->fts_path, local_cksum) == EPKG_OK) {

				if (strcmp(cksum, local_cksum) != 0) {
					ret = add_to_dellist(&dl, CKSUM_MISMATCH, ent->fts_path,
							     origin, NULL, NULL);
				}
			}
		}

		if (ret != EPKG_OK && ret != EPKG_END) {
			retcode = EX_OSERR; /* out of memory */
			goto cleanup;
		}

		pkgdb_it_free(it);
	}

	if (STAILQ_EMPTY(&dl)) {
		if (!quiet)
			printf("Nothing to do.\n");
		retcode = EX_OK;
		goto cleanup;
	}

	if (dry_run || !yes || !quiet)
		display_dellist(&dl, cachedir);

	if (!dry_run) {
		if (!yes)
			yes = query_yesno(
				"\nProceed with cleaning the cache [y/N]: ");
		if (yes)
			retcode = delete_dellist(&dl);
	} else
		retcode = EX_OK;

cleanup:
	pkg_manifest_keys_free(keys);
	free_dellist(&dl);

	pkg_free(pkg);
	pkg_free(p);
	if (fts != NULL)
		fts_close(fts);
	if (db != NULL)
		pkgdb_close(db);

	return (retcode);
}
