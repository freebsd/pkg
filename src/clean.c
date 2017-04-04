/*-
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2016 Baptiste Daroussin <bapt@FreeBSD.org>
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
/* For MIN */
#include <sys/param.h>

#ifdef HAVE_CAPSICUM
#include <sys/capability.h>
#endif

#include <assert.h>
#include <err.h>
#include <getopt.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <pkg.h>
#include <stdbool.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <khash.h>
#include <kvec.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#include <bsd_compat.h>

#include "pkgcli.h"

KHASH_MAP_INIT_STR(sum, char *);
typedef kvec_t(char *) dl_list;

#define OUT_OF_DATE	(1U<<0)
#define REMOVED		(1U<<1)
#define CKSUM_MISMATCH	(1U<<2)
#define SIZE_MISMATCH	(1U<<3)
#define ALL		(1U<<4)

static size_t
add_to_dellist(int fd, dl_list *dl, const char *cachedir, const char *path)
{
	static bool first_entry = true;
	struct stat st;
	char *store_path;
	const char *relpath;
	size_t sz = 0;

	assert(path != NULL);

	store_path = strdup(path);

	if (!quiet) {
		if (first_entry) {
			first_entry = false;
			printf("The following package files will be deleted:"
			    "\n");
		}
		printf("\t%s\n", store_path);
	}

	relpath = path + strlen(cachedir) + 1;
	if (fstatat(fd, relpath, &st, AT_SYMLINK_NOFOLLOW) != -1 && S_ISREG(st.st_mode))
		sz = st.st_size;
	kv_push(char *, *dl, store_path);

	return (sz);
}

static void
free_dellist(dl_list *dl)
{
	unsigned int i;

	for (i = 0; i < kv_size(*dl); i++)
		free(kv_A(*dl, i));
	kv_destroy(*dl);
}

static int
delete_dellist(int fd, const char *cachedir,  dl_list *dl, int total)
{
	struct stat st;
	int retcode = EX_OK;
	int flag = 0;
	unsigned int count = 0, processed = 0;
	char *file, *relpath;

	count = kv_size(*dl);
	progressbar_start("Deleting files");
	for (int i = 0; i < kv_size(*dl); i++) {
		flag = 0;
		relpath = file = kv_A(*dl, i);
		relpath += strlen(cachedir) + 1;
		if (fstatat(fd, relpath, &st, AT_SYMLINK_NOFOLLOW) == -1) {
			++processed;
			progressbar_tick(processed, total);
			warn("can't stat %s", file);
			continue;
		}
		if (S_ISDIR(st.st_mode))
			flag = AT_REMOVEDIR;
		if (unlinkat(fd, relpath, flag) == -1) {
			warn("unlink(%s)", file);
			retcode = EX_SOFTWARE;
		}
		free(file);
		kv_A(*dl, i) = NULL;
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

static kh_sum_t *
populate_sums(struct pkgdb *db)
{
	struct pkg *p = NULL;
	struct pkgdb_it *it = NULL;
	const char *sum;
	char *cksum;
	size_t slen;
	kh_sum_t *suml = NULL;
	khint_t k;
	int ret;

	suml = kh_init_sum();
	it = pkgdb_repo_search(db, "*", MATCH_GLOB, FIELD_NAME, FIELD_NONE, NULL);
	while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(p, PKG_CKSUM, &sum);
		slen = MIN(strlen(sum), PKG_FILE_CKSUM_CHARS);
		cksum = strndup(sum, slen);
		k = kh_put_sum(suml, cksum, &ret);
		if (ret != 0)
			kh_value(suml, k) = cksum;
	}

	return (suml);
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

static int
recursive_analysis(int fd, struct pkgdb *db, const char *dir,
    const char *cachedir, dl_list *dl, kh_sum_t **sumlist, bool all,
    size_t *total)
{
	DIR *d;
	struct dirent *ent;
	int newfd, tmpfd;
	char path[MAXPATHLEN], csum[PKG_FILE_CKSUM_CHARS + 1],
		link_buf[MAXPATHLEN];
	const char *name;
	ssize_t link_len;
	size_t nbfiles = 0, added = 0;
	khint_t k;

	tmpfd = dup(fd);
	d = fdopendir(tmpfd);
	if (d == NULL) {
		close(tmpfd);
		warnx("Impossible to open the directory %s", dir);
		return (0);
	}

	while ((ent = readdir(d)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		if (ent->d_type == DT_DIR) {
			nbfiles++;
			newfd = openat(fd, ent->d_name, O_DIRECTORY|O_CLOEXEC, 0);
			if (newfd == -1) {
				warnx("Impossible to open the directory %s",
				    path);
				continue;
			}
			if (recursive_analysis(newfd, db, path, cachedir, dl,
			    sumlist, all, total) == 0 || all) {
				add_to_dellist(fd, dl, cachedir, path);
				added++;
			}
			close(newfd);
			continue;
		}
		if (ent->d_type != DT_LNK && ent->d_type != DT_REG)
			continue;
		nbfiles++;
		if (all) {
			*total += add_to_dellist(fd, dl, cachedir, path);
			continue;
		}
		if (*sumlist == NULL) {
			*sumlist = populate_sums(db);
		}
		name = ent->d_name;
		if (ent->d_type == DT_LNK) {
			/* Dereference the symlink and check it for being
			 * recognized checksum file, or delete the symlink
			 * later. */
			if ((link_len = readlinkat(fd, ent->d_name, link_buf,
			    sizeof(link_buf))) == -1)
				continue;
			link_buf[link_len - 1] = '\0';
			name = link_buf;
		}

		k = kh_end(*sumlist);
		if (extract_filename_sum(name, csum))
			k = kh_get_sum(*sumlist, csum);
		if (k == kh_end(*sumlist)) {
			added++;
			*total += add_to_dellist(fd, dl, cachedir, path);
		}
	}
	closedir(d);
	return (nbfiles - added);
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
	kh_sum_t	*sumlist = NULL;
	dl_list		 dl;
	const char	*cachedir;
	bool		 all = false;
	int		 retcode;
	int		 ch;
	int		 cachefd = -1;
	size_t		 total = 0;
	char		 size[8];
	char		*cksum;
	struct pkg_manifest_key *keys = NULL;
#ifdef HAVE_CAPSICUM
	cap_rights_t rights;
#endif

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

	cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));
	cachefd = open(cachedir, O_DIRECTORY|O_CLOEXEC);
	if (cachefd == -1) {
		warn("Impossible to open %s", cachedir);
		return (errno == ENOENT ? EX_OK : EX_IOERR);
	}

	retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_REPO);

	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to clean old packages");
		close(cachefd);
		return (EX_NOPERM);
	} else if (retcode == EPKG_ENODB) {
		warnx("No package database installed.  Nothing to do!");
		close(cachefd);
		return (EX_OK);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		close(cachefd);
		return (EX_SOFTWARE);
	}

	retcode = EX_SOFTWARE;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		close(cachefd);
		return (EX_IOERR);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		close(cachefd);
		warnx("Cannot get a read lock on a database, it is locked by "
		    "another process");
		return (EX_TEMPFAIL);
	}

#ifdef HAVE_CAPSICUM
		cap_rights_init(&rights, CAP_READ, CAP_LOOKUP, CAP_FSTATFS,
		    CAP_FSTAT, CAP_UNLINKAT);
		if (cap_rights_limit(cachefd, &rights) < 0 && errno != ENOSYS ) {
			warn("cap_rights_limit() failed");
			close(cachefd);
			return (EX_SOFTWARE);
		}

		if (cap_enter() < 0 && errno != ENOSYS) {
			warn("cap_enter() failed");
			close(cachefd);
			return (EX_SOFTWARE);
		}
#endif

	kv_init(dl);

	/* Build the list of out-of-date or obsolete packages */

	pkg_manifest_keys_new(&keys);
	recursive_analysis(cachefd, db, cachedir, cachedir, &dl, &sumlist, all,
	    &total);
	if (sumlist != NULL) {
		kh_foreach_value(sumlist, cksum, free(cksum));
		kh_destroy_sum(sumlist);
	}

	if (kv_size(dl) == 0) {
		if (!quiet)
			printf("Nothing to do.\n");
		retcode = EX_OK;
		goto cleanup;
	}

	humanize_number(size, sizeof(size), total, "B",
	    HN_AUTOSCALE, HN_IEC_PREFIXES);

	if (!quiet)
		printf("The cleanup will free %s\n", size);
	if (!dry_run) {
			if (query_yesno(false,
			  "\nProceed with cleaning the cache? ")) {
				retcode = delete_dellist(cachefd, cachedir, &dl, kv_size(dl));
			}
	} else {
		retcode = EX_OK;
	}

cleanup:
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);
	pkg_manifest_keys_free(keys);
	free_dellist(&dl);

	if (cachefd != -1)
		close(cachefd);

	return (retcode);
}
