/*-
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2016-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/stat.h>
/* For MIN */
#include <sys/param.h>

#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
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
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#include <bsd_compat.h>

#include "pkgcli.h"
#include "pkghash.h"
#include "xmalloc.h"
#include "pkg/vec.h"

#define OUT_OF_DATE	(1U<<0)
#define REMOVED		(1U<<1)
#define CKSUM_MISMATCH	(1U<<2)
#define SIZE_MISMATCH	(1U<<3)
#define ALL		(1U<<4)

static size_t
add_to_dellist(int fd, charv_t *dl, const char *cachedir, const char *path)
{
	static bool first_entry = true;
	struct stat st;
	char *store_path;
	const char *relpath;
	size_t sz = 0;

	assert(path != NULL);

	store_path = xstrdup(path);

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
	vec_push(dl, store_path);

	return (sz);
}

static int
delete_dellist(int fd, const char *cachedir,  charv_t *dl)
{
	struct stat st;
	int retcode = EXIT_SUCCESS;
	int flag = 0;
	unsigned int count = 0, processed = 0;
	char *file, *relpath;

	count = dl->len;
	progressbar_start("Deleting files");
	vec_foreach(*dl, i) {
		flag = 0;
		relpath = file = dl->d[i];
		relpath += strlen(cachedir) + 1;
		if (fstatat(fd, relpath, &st, AT_SYMLINK_NOFOLLOW) == -1) {
			++processed;
			progressbar_tick(processed, dl->len);
			warn("can't stat %s", file);
			continue;
		}
		if (S_ISDIR(st.st_mode))
			flag = AT_REMOVEDIR;
		if (unlinkat(fd, relpath, flag) == -1) {
			warn("unlink(%s)", file);
			retcode = EXIT_FAILURE;
		}
		free(file);
		dl->d[i] = NULL;
		++processed;
		progressbar_tick(processed, dl->len);
	}
	progressbar_tick(processed, dl->len);

	if (!quiet) {
		if (retcode != EXIT_SUCCESS)
			printf("%d package%s could not be deleted\n",
			      count, count > 1 ? "s" : "");
	}
	return (retcode);
}

static pkghash *
populate_sums(struct pkgdb *db)
{
	struct pkg *p = NULL;
	struct pkgdb_it *it = NULL;
	const char *sum;
	char *cksum;
	size_t slen;
	pkghash *suml = NULL;

	suml = pkghash_new();
	it = pkgdb_repo_search(db, "*", MATCH_GLOB, FIELD_NAME, FIELD_NONE, NULL);
	while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(p, PKG_ATTR_CKSUM, &sum);
		slen = MIN(strlen(sum), PKG_FILE_CKSUM_CHARS);
		cksum = strndup(sum, slen);
		pkghash_safe_add(suml, cksum, NULL, NULL);
		free(cksum);
	}
	pkgdb_it_free(it);

	return (suml);
}

/*
 * Extract hash from filename in format <name>-<version>~<hash>.txz
 */
static bool
extract_filename_sum(const char *fname, char sum[])
{
	const char *tilde_pos, *dot_pos;

	dot_pos = strrchr(fname, '.');
	if (dot_pos == NULL)
		dot_pos = fname + strlen(fname);

	tilde_pos = strrchr(fname, '~');
	/* XXX Legacy fallback; remove eventually. */
	if (tilde_pos == NULL)
		tilde_pos = strrchr(fname, '-');
	if (tilde_pos == NULL)
		return (false);
	else if (dot_pos < tilde_pos)
		dot_pos = fname + strlen(fname);

	if (dot_pos - tilde_pos != PKG_FILE_CKSUM_CHARS + 1)
		return (false);

	strlcpy(sum, tilde_pos + 1, PKG_FILE_CKSUM_CHARS + 1);
	return (true);
}

static int
recursive_analysis(int fd, struct pkgdb *db, const char *dir,
    const char *cachedir, charv_t *dl, pkghash **sumlist, bool all,
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
	pkghash_entry *e;

	tmpfd = dup(fd);
	d = fdopendir(tmpfd);
	if (d == NULL) {
		close(tmpfd);
		warnx("Impossible to open the directory %s", dir);
		return (0);
	}

	while ((ent = readdir(d)) != NULL) {
		if (STREQ(ent->d_name, ".") ||
		    STREQ(ent->d_name, ".."))
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
			if (link_len > 0 )
				link_buf[link_len - 1] = '\0';
			else
				link_buf[0]='\0';
			name = link_buf;
		}

		e = NULL;
		if (extract_filename_sum(name, csum)) {
			e = pkghash_get(*sumlist, csum);
		}
		if (e == NULL) {
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
	pkghash		*sumlist = NULL;
	charv_t		 dl = vec_init();
	const char	*cachedir;
	bool		 all = false;
	int		 retcode;
	int		 ch;
	int		 cachefd = -1;
	size_t		 total = 0;
	char		 size[8];
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
			return (EXIT_FAILURE);
		}
	}

	cachedir = pkg_get_cachedir();
	cachefd = pkg_get_cachedirfd();
	if (cachefd == -1) {
		warn("Impossible to open %s", cachedir);
		return (errno == ENOENT ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_REPO);

	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to clean old packages");
		close(cachefd);
		return (EXIT_FAILURE);
	} else if (retcode == EPKG_ENODB) {
		warnx("No package database installed.  Nothing to do!");
		close(cachefd);
		return (EXIT_SUCCESS);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		close(cachefd);
		return (EXIT_FAILURE);
	}

	retcode = EXIT_FAILURE;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		close(cachefd);
		return (EXIT_FAILURE);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		close(cachefd);
		warnx("Cannot get a read lock on a database, it is locked by "
		    "another process");
		return (EXIT_FAILURE);
	}

#ifdef HAVE_CAPSICUM
		cap_rights_init(&rights, CAP_READ, CAP_LOOKUP, CAP_FSTATFS,
		    CAP_FSTAT, CAP_UNLINKAT);
		if (cap_rights_limit(cachefd, &rights) < 0 && errno != ENOSYS ) {
			warn("cap_rights_limit() failed");
			close(cachefd);
			return (EXIT_FAILURE);
		}

#ifndef PKG_COVERAGE
		if (cap_enter() < 0 && errno != ENOSYS) {
			warn("cap_enter() failed");
			close(cachefd);
			return (EXIT_FAILURE);
		}
#endif
#endif

	/* Build the list of out-of-date or obsolete packages */

	recursive_analysis(cachefd, db, cachedir, cachedir, &dl, &sumlist, all,
	    &total);
	pkghash_destroy(sumlist);

	if (dl.len == 0) {
		if (!quiet)
			printf("Nothing to do.\n");
		retcode = EXIT_SUCCESS;
		goto cleanup;
	}

	humanize_number(size, sizeof(size), total, "B",
	    HN_AUTOSCALE, HN_IEC_PREFIXES);

	if (!quiet)
		printf("The cleanup will free %s\n", size);
	if (!dry_run) {
			if (query_yesno(false,
			  "\nProceed with cleaning the cache? ")) {
				retcode = delete_dellist(cachefd, cachedir, &dl);
			}
	} else {
		retcode = EXIT_SUCCESS;
	}

cleanup:
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);
	vec_free_and_free(&dl, free);

	if (cachefd != -1)
		close(cachefd);

	return (retcode);
}
