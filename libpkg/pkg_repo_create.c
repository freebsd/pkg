/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 *
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <archive_entry.h>
#include <assert.h>
#include <fts.h>
#include <libgen.h>
#include <sqlite3.h>
#include <string.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <stdbool.h>
#include <sysexits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/repodb.h"
#include "private/thd_repo.h"

struct digest_list_entry {
	char *origin;
	char *digest;
	long manifest_pos;
	long files_pos;
	long manifest_length;
	struct digest_list_entry *prev, *next;
};

struct pkg_conflict_bulk {
	struct pkg_conflict *conflicts;
	char *file;
	UT_hash_handle hh;
};

static void
pkg_read_pkg_file(void *data)
{
	struct thd_data *d = (struct thd_data*) data;
	struct pkg_result *r;
	struct pkg_manifest_key *keys = NULL;

	FTSENT *fts_ent = NULL;
	char fts_accpath[MAXPATHLEN];
	char fts_path[MAXPATHLEN];
	char fts_name[MAXPATHLEN];
	off_t st_size;
	int fts_info, flags;

	char *ext = NULL;
	char *pkg_path;

	pkg_manifest_keys_new(&keys);

	for (;;) {
		fts_ent = NULL;

		/*
		 * Get a file to read from.
		 * Copy the data we need from the fts entry localy because as soon as
		 * we unlock the fts_m mutex, we can not access it.
		 */
		pthread_mutex_lock(&d->fts_m);
		if (!d->stop)
			fts_ent = fts_read(d->fts);
		if (fts_ent != NULL) {
			strlcpy(fts_accpath, fts_ent->fts_accpath, sizeof(fts_accpath));
			strlcpy(fts_path, fts_ent->fts_path, sizeof(fts_path));
			strlcpy(fts_name, fts_ent->fts_name, sizeof(fts_name));
			st_size = fts_ent->fts_statp->st_size;
			fts_info = fts_ent->fts_info;
		}
		pthread_mutex_unlock(&d->fts_m);

		/* There is no more jobs, exit the main loop. */
		if (fts_ent == NULL)
			break;

		/* Skip everything that is not a file */
		if (fts_info != FTS_F)
			continue;

		ext = strrchr(fts_name, '.');

		if (ext == NULL)
			continue;

		if (strcmp(ext, ".tgz") != 0 &&
				strcmp(ext, ".tbz") != 0 &&
				strcmp(ext, ".txz") != 0 &&
				strcmp(ext, ".tar") != 0)
			continue;

		*ext = '\0';

		if (strcmp(fts_name, repo_db_archive) == 0 ||
			strcmp(fts_name, repo_packagesite_archive) == 0 ||
			strcmp(fts_name, repo_filesite_archive) == 0 ||
			strcmp(fts_name, repo_digests_archive) == 0 ||
			strcmp(fts_name, repo_conflicts_archive) == 0)
			continue;
		*ext = '.';

		pkg_path = fts_path;
		pkg_path += strlen(d->root_path);
		while (pkg_path[0] == '/')
			pkg_path++;

		r = calloc(1, sizeof(struct pkg_result));
		strlcpy(r->path, pkg_path, sizeof(r->path));

		if (d->read_files)
			flags = PKG_OPEN_MANIFEST_ONLY;
		else
			flags = PKG_OPEN_MANIFEST_ONLY | PKG_OPEN_MANIFEST_COMPACT;

		if (pkg_open(&r->pkg, fts_accpath, keys, flags) != EPKG_OK) {
			r->retcode = EPKG_WARN;
		} else {
			sha256_file(fts_accpath, r->cksum);
			pkg_set(r->pkg, PKG_CKSUM, r->cksum,
			    PKG_REPOPATH, pkg_path,
			    PKG_PKGSIZE, st_size);
		}


		/* Add result to the FIFO and notify */
		pthread_mutex_lock(&d->results_m);
		while (d->num_results >= d->max_results) {
			pthread_cond_wait(&d->has_room, &d->results_m);
		}
		DL_APPEND(d->results, r);
		d->num_results++;
		pthread_cond_signal(&d->has_result);
		pthread_mutex_unlock(&d->results_m);
	}

	/*
	 * This thread is about to exit.
	 * Notify the main thread that we are done.
	 */
	pthread_mutex_lock(&d->results_m);
	d->thd_finished++;
	pthread_cond_signal(&d->has_result);
	pthread_mutex_unlock(&d->results_m);
	pkg_manifest_keys_free(keys);
}

static int
pkg_digest_sort_compare_func(struct digest_list_entry *d1,
		struct digest_list_entry *d2)
{
	return strcmp(d1->origin, d2->origin);
}

static void
pkg_repo_new_conflict(const char *uniqueid, struct pkg_conflict_bulk *bulk)
{
	struct pkg_conflict *new;

	pkg_conflict_new(&new);
	sbuf_set(&new->uniqueid, uniqueid);

	HASH_ADD_KEYPTR(hh, bulk->conflicts,
			pkg_conflict_uniqueid(new),
			sbuf_size(new->uniqueid), new);
}

static void
pkg_repo_write_conflicts (struct pkg_conflict_bulk *bulk, FILE *out)
{
	struct pkg_conflict_bulk	*pkg_bulk = NULL, *cur, *tmp, *s;
	struct pkg_conflict	*c1, *c1tmp, *c2, *c2tmp, *ctmp;
	bool new;

	/*
	 * Here we reorder bulk hash from hash by file
	 * to hash indexed by a package, so we iterate over the
	 * original hash and create a new hash indexed by package name
	 */

	HASH_ITER (hh, bulk, cur, tmp) {
		HASH_ITER (hh, cur->conflicts, c1, c1tmp) {
			HASH_FIND_STR(pkg_bulk, sbuf_get(c1->uniqueid), s);
			if (s == NULL) {
				/* New entry required */
				s = malloc(sizeof(struct pkg_conflict_bulk));
				if (s == NULL) {
					pkg_emit_errno("malloc", "struct pkg_conflict_bulk");
					goto out;
				}
				memset(s, 0, sizeof(struct pkg_conflict_bulk));
				s->file = sbuf_get(c1->uniqueid);
				HASH_ADD_KEYPTR(hh, pkg_bulk, s->file, strlen(s->file), s);
			}
			/* Now add all new entries from this file to this conflict structure */
			HASH_ITER (hh, cur->conflicts, c2, c2tmp) {
				new = true;
				if (strcmp(sbuf_get(c1->uniqueid), sbuf_get(c2->uniqueid)) == 0)
					continue;

				HASH_FIND_STR(s->conflicts, sbuf_get(c2->uniqueid), ctmp);
				if (ctmp == NULL)
					pkg_repo_new_conflict(sbuf_get(c2->uniqueid), s);
			}
		}
	}

	HASH_ITER (hh, pkg_bulk, cur, tmp) {
		fprintf(out, "%s:", cur->file);
		HASH_ITER (hh, cur->conflicts, c1, c1tmp) {
			if (c1->hh.next != NULL)
				fprintf(out, "%s,", sbuf_get(c1->uniqueid));
			else
				fprintf(out, "%s\n", sbuf_get(c1->uniqueid));
		}
	}
out:
	HASH_ITER (hh, pkg_bulk, cur, tmp) {
		HASH_ITER (hh, cur->conflicts, c1, c1tmp) {
			HASH_DEL(cur->conflicts, c1);
			sbuf_free(c1->uniqueid);
			free(c1);
		}
		HASH_DEL(pkg_bulk, cur);
		free(cur);
	}
	return;
}

struct pkg_fts_item {
	char *fts_accpath;
	char *pkg_path;
	char *fts_name;
	off_t fts_size;
	int fts_info;
	struct pkg_fts_item *next;
};

static struct pkg_fts_item*
pkg_create_repo_fts_new(FTSENT *fts, const char *root_path)
{
	struct pkg_fts_item *item;
	char *pkg_path;

	item = malloc(sizeof(*item));
	if (item != NULL) {
		item->fts_accpath = strdup(fts->fts_accpath);
		item->fts_name = strdup(fts->fts_name);
		item->fts_size = fts->fts_statp->st_size;
		item->fts_info = fts->fts_info;

		pkg_path = fts->fts_path;
		pkg_path += strlen(root_path);
		while (pkg_path[0] == '/')
			pkg_path++;

		item->pkg_path = strdup(pkg_path);
	}
	else {
		pkg_emit_errno("malloc", "struct pkg_fts_item");
	}

	return (item);
}

static void
pkg_create_repo_fts_free(struct pkg_fts_item *item)
{
	free(item->fts_accpath);
	free(item->pkg_path);
	free(item->fts_name);
	free(item);
}

static int
pkg_create_repo_read_fts(struct pkg_fts_item **items, FTS *fts,
	const char *repopath, size_t *plen)
{
	FTSENT *fts_ent;
	struct pkg_fts_item *fts_cur;
	char *ext;

	errno = 0;

	while ((fts_ent = fts_read(fts)) != NULL) {
		/* Skip everything that is not a file */
		if (fts_ent->fts_info != FTS_F)
			continue;

		ext = strrchr(fts_ent->fts_name, '.');

		if (ext == NULL)
			continue;

		if (strcmp(ext, ".tgz") != 0 &&
						strcmp(ext, ".tbz") != 0 &&
						strcmp(ext, ".txz") != 0 &&
						strcmp(ext, ".tar") != 0)
			continue;

		*ext = '\0';

		if (strcmp(fts_ent->fts_name, repo_db_archive) == 0 ||
						strcmp(fts_ent->fts_name, repo_packagesite_archive) == 0 ||
						strcmp(fts_ent->fts_name, repo_filesite_archive) == 0 ||
						strcmp(fts_ent->fts_name, repo_digests_archive) == 0 ||
						strcmp(fts_ent->fts_name, repo_conflicts_archive) == 0) {
			*ext = '.';
			continue;
		}

		*ext = '.';
		fts_cur = pkg_create_repo_fts_new(fts_ent, repopath);
		if (fts_cur == NULL)
			return (EPKG_FATAL);

		LL_PREPEND(*items, fts_cur);
		(*plen) ++;
	}

	if (errno != 0) {
		pkg_emit_errno("fts_read", "pkg_create_repo_read_fts");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
pkg_create_repo_worker(struct pkg_fts_item *start, size_t nelts,
	const char *mlfile, const char *flfile, int pip)
{
	pid_t pid;
	int mfd, ffd;
	bool read_files = (flfile != NULL);
	int flags, ret = EPKG_OK;
	size_t cur_job = 0;
	struct pkg_fts_item *cur;
	struct pkg *pkg = NULL;
	struct pkg_manifest_key *keys = NULL;
	char checksum[SHA256_DIGEST_LENGTH * 2 + 1], *mdigest;
	char digestbuf[1024];

	struct sbuf *b = sbuf_new_auto();

	mfd = open(mlfile, O_APPEND|O_CREAT|O_WRONLY, 00644);
	if (mfd == -1) {
		pkg_emit_errno("pkg_create_repo_worker", "open");
		return (EPKG_FATAL);
	}

	if (read_files) {
		ffd = open(flfile, O_APPEND|O_CREAT|O_WRONLY, 00644);
		if (ffd == -1) {
			pkg_emit_errno("pkg_create_repo_worker", "open");
			return (EPKG_FATAL);
		}
	}

	pid = fork();
	switch(pid) {
	case -1:
		pkg_emit_errno("pkg_create_repo_worker", "fork");
		return (EPKG_FATAL);
		break;
	case 0:
		break;
	default:
		/* Parent */
		close(mfd);
		if (read_files)
			close(ffd);

		return (EPKG_OK);
		break;
	}

	if (read_files)
		flags = PKG_OPEN_MANIFEST_ONLY;
	else
		flags = PKG_OPEN_MANIFEST_ONLY | PKG_OPEN_MANIFEST_COMPACT;

	LL_FOREACH(start, cur) {
		if (cur_job >= nelts)
			break;

		if (pkg_open(&pkg, cur->fts_accpath, keys, flags) == EPKG_OK) {
			int r;
			off_t mpos, fpos = 0;
			size_t mlen;
			const char *origin;

			sha256_file(cur->fts_accpath, checksum);
			pkg_set(pkg, PKG_CKSUM, checksum,
				PKG_REPOPATH, cur->pkg_path,
				PKG_PKGSIZE, cur->fts_size);
			pkg_get(pkg, PKG_ORIGIN, &origin);

			/*
			 * TODO: use pkg_checksum for new manifests
			 */
			sbuf_clear(b);
			pkg_emit_manifest_sbuf(pkg, b, PKG_MANIFEST_EMIT_COMPACT, &mdigest);
			mlen = sbuf_len(b);
			sbuf_finish(b);

			if (flock(mfd, LOCK_EX) == -1) {
				pkg_emit_errno("pkg_create_repo_worker", "flock");
				ret = EPKG_FATAL;
				goto cleanup;
			}

			mpos = lseek(mfd, 0, SEEK_END);

			if (write(mfd, sbuf_data(b), sbuf_len(b)) == -1) {
				pkg_emit_errno("pkg_create_repo_worker", "write");
				ret = EPKG_FATAL;
				flock(mfd, LOCK_UN);
				goto cleanup;
			}

			flock(mfd, LOCK_UN);

			if (read_files) {
				FILE *fl;

				if (flock(ffd, LOCK_EX) == -1) {
					pkg_emit_errno("pkg_create_repo_worker", "flock");
					ret = EPKG_FATAL;
					goto cleanup;
				}
				fpos = lseek(ffd, 0, SEEK_END);
				fl = fdopen(ffd, "a");
				pkg_emit_filelist(pkg, fl);
				fclose(fl);

				flock(ffd, LOCK_UN);
			}

			r = snprintf(digestbuf, sizeof(digestbuf), "%s:%s:%ld:%ld:%ld\n",
				origin, mdigest,
				(long)mpos,
				(long)fpos,
				(long)mlen);

			write(pip, digestbuf, r);
		}
		cur_job ++;
	}

cleanup:
	pkg_manifest_keys_free(keys);

	close(pip);
	close(mfd);
	if (read_files)
		close(ffd);

	exit(ret);
}

static int
pkg_create_repo_read_pipe(int fd, struct digest_list_entry **dlist)
{
	struct digest_list_entry *dig;
	char buf[1024];
	int r, i, start;
	enum {
		s_set_origin = 0,
		s_set_digest,
		s_set_mpos,
		s_set_fpos,
		s_set_mlen
	} state = 0;

	for (;;) {
		r = read(fd, buf, sizeof(buf));

		if (r == -1) {
			if (errno == EINTR)
				continue;

			pkg_emit_errno("pkg_create_repo_read_pipe", "read");
			return (EPKG_FATAL);
		}
		else if (r == 0)
			return (EPKG_END);

		break;
	}

	/*
	 * XXX: can parse merely full lines
	 */
	start = 0;
	for (i = 0; i < r; i ++) {
		if (buf[i] == ':') {
			switch(state) {
			case s_set_origin:
				dig = malloc(sizeof(*dig));
				dig->origin = malloc(i - start + 1);
				strlcpy(dig->origin, &buf[start], i - start + 1);
				state = s_set_digest;
				break;
			case s_set_digest:
				dig->digest = malloc(i - start + 1);
				strlcpy(dig->digest, &buf[start], i - start + 1);
				state = s_set_mpos;
				break;
			case s_set_mpos:
				dig->manifest_pos = strtol(&buf[start], NULL, 10);
				state = s_set_fpos;
				break;
			case s_set_fpos:
				dig->files_pos = strtol(&buf[start], NULL, 10);
				state = s_set_mlen;
				break;
			case s_set_mlen:
				/* Record should actually not finish with ':' */
				dig->manifest_length = strtol(&buf[start], NULL, 10);
				state = s_set_origin;
				break;
			}
			start = i + 1;
		}
		else if (buf[i] == '\n') {
			dig->manifest_length = strtol(&buf[start], NULL, 10);
			DL_APPEND(*dlist, dig);
			state = s_set_origin;
			start = i + 1;
			break;
		}
	}

	return (EPKG_OK);
}

int
pkg_create_repo(char *path, const char *output_dir, bool filelist,
		void (progress)(struct pkg *pkg, void *data), void *data)
{
	FTS *fts = NULL;
	struct pkg_fts_item *fts_items = NULL, *fts_cur;

	struct pkg_conflict *c, *ctmp;
	struct pkg_conflict_bulk *conflicts = NULL, *curcb, *tmpcb;
	int num_workers, i;
	size_t len, tasks_per_worker, ntask;
	struct digest_list_entry *dlist = NULL, *cur_dig, *dtmp;
	struct pollfd *pfd = NULL;
	int cur_pipe[2], fd;

	int retcode = EPKG_OK;

	char *repopath[2];
	char packagesite[MAXPATHLEN],
		 filesite[MAXPATHLEN],
		 repodb[MAXPATHLEN];
	FILE *mandigests = NULL;

	if (!is_dir(path)) {
		pkg_emit_error("%s is not a directory", path);
		return (EPKG_FATAL);
	}

	errno = 0;
	if (!is_dir(output_dir)) {
		/* Try to create dir */
		if (errno == ENOENT) {
			if (mkdir(output_dir, 00755) == -1) {
				pkg_emit_error("cannot create output directory %s: %s",
					output_dir, strerror(errno));
				return (EPKG_FATAL);
			}
		}
		else {
			pkg_emit_error("%s is not a directory", output_dir);
			return (EPKG_FATAL);
		}
	}

	repopath[0] = path;
	repopath[1] = NULL;

	len = sizeof(num_workers);
	if (sysctlbyname("hw.ncpu", &num_workers, &len, NULL, 0) == -1)
		num_workers = 6;

	if ((fts = fts_open(repopath, FTS_PHYSICAL|FTS_NOCHDIR, NULL)) == NULL) {
		pkg_emit_errno("fts_open", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	snprintf(packagesite, sizeof(packagesite), "%s/%s", output_dir,
	    repo_packagesite_file);
	if ((fd = open(packagesite, O_CREAT|O_TRUNC|O_WRONLY, 00644)) == -1) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}
	close(fd);
	if (filelist) {
		snprintf(filesite, sizeof(filesite), "%s/%s", output_dir,
		    repo_filesite_file);
		if ((fd = open(packagesite, O_CREAT|O_TRUNC|O_WRONLY, 00644)) == -1) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
		close(fd);
	}
	snprintf(repodb, sizeof(repodb), "%s/%s", output_dir,
	    repo_digests_file);
	if ((mandigests = fopen(repodb, "w")) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	len = 0;

	pkg_create_repo_read_fts(&fts_items, fts, path, &len);

	if (len == 0) {
		/* Nothing to do */
		pkg_emit_error("No package files have been found");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	/* Split items over all workers */
	num_workers = MIN(num_workers, len);
	tasks_per_worker = ceil((double)len / num_workers);

	/* Launch workers */
	pfd = calloc(num_workers, sizeof(struct pollfd));
	ntask = 0;
	LL_FOREACH(fts_items, fts_cur) {
		if (ntask % tasks_per_worker == 0) {
			/* Create new worker */
			int nworker = ntask / tasks_per_worker;

			if (pipe(cur_pipe) == -1) {
				pkg_emit_errno("pkg_create_repo", "pipe");
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (pkg_create_repo_worker(fts_cur, tasks_per_worker, packagesite,
				filelist ? filesite : NULL, cur_pipe[1]) == EPKG_FATAL) {
				close(cur_pipe[0]);
				close(cur_pipe[1]);
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			pfd[nworker].fd = cur_pipe[0];
			pfd[nworker].events = POLL_IN;
			close(cur_pipe[1]);
		}
		ntask ++;
	}

	while(num_workers > 0) {
		retcode = poll(pfd, num_workers, -1);
		if (retcode == -1) {
			if (errno == EINTR) {
				continue;
			}
			else {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}
		else if (retcode > 0) {
			for (i = 0; i < num_workers; i ++) {
				if (pfd[i].revents & POLL_IN) {
					if (pkg_create_repo_read_pipe(pfd[i].fd, &dlist) != EPKG_OK) {
						/*
						 * Wait for the worker finished
						 */
						int st;

						if (wait(&st) == -1)
							pkg_emit_errno("pkg_create_repo", "wait");

						num_workers --;
					}
				}
			}
		}
	}

	retcode = EPKG_OK;

	/* Now sort all digests */
	DL_SORT(dlist, pkg_digest_sort_compare_func);

	/*
	 * XXX: it is not used actually
	 */
#if 0
	pkg_repo_write_conflicts(conflicts, fconflicts);
#endif

cleanup:
	HASH_ITER (hh, conflicts, curcb, tmpcb) {
		HASH_ITER (hh, curcb->conflicts, c, ctmp) {
			sbuf_free(c->uniqueid);
			HASH_DEL(curcb->conflicts, c);
			free(c);
		}
		HASH_DEL(conflicts, curcb);
		free(curcb);
	}
	/* Close pipes */
	if (pfd != NULL) {
		for (i = 0; i < num_workers; i ++)
			close(pfd[i].fd);
		free(pfd);
	}
	if (fts != NULL)
		fts_close(fts);

	LL_FREE(fts_items, pkg_create_repo_fts_free);
	LL_FOREACH_SAFE(dlist, cur_dig, dtmp) {
		fprintf(mandigests, "%s:%s:%ld:%ld:%ld\n", cur_dig->origin,
		    cur_dig->digest, cur_dig->manifest_pos, cur_dig->files_pos,
		    cur_dig->manifest_length);
		free(cur_dig->digest);
		free(cur_dig->origin);
		free(cur_dig);
	}

	if (mandigests != NULL)
		fclose(mandigests);

	return (retcode);
}


static int
pkg_repo_sign(char *path, char **argv, int argc, struct sbuf **sig, struct sbuf **cert)
{
	FILE *fp;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	struct sbuf *cmd = NULL;
	struct sbuf *buf = NULL;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int i, ret = EPKG_OK;

	if (sha256_file(path, sha256) != EPKG_OK)
		return (EPKG_FATAL);

	cmd = sbuf_new_auto();

	for (i = 0; i < argc; i++) {
		if (strspn(argv[i], " \t\n") > 0)
			sbuf_printf(cmd, " \"%s\" ", argv[i]);
		else
			sbuf_printf(cmd, " %s ", argv[i]);
	}
	sbuf_done(cmd);

	if ((fp = popen(sbuf_data(cmd), "r+")) == NULL) {
		ret = EPKG_FATAL;
		goto done;
	}

	fprintf(fp, "%s\n", sha256);

	if (*sig == NULL)
		*sig = sbuf_new_auto();
	if (*cert == NULL)
		*cert = sbuf_new_auto();

	while ((linelen = getline(&line, &linecap, fp)) > 0 ) {
		if (strcmp(line, "SIGNATURE\n") == 0) {
			buf = *sig;
			continue;
		} else if (strcmp(line, "CERT\n") == 0) {
			buf = *cert;
			continue;
		} else if (strcmp(line, "END\n") == 0) {
			break;
		}
		if (buf != NULL)
			sbuf_bcat(buf, line, linelen);
	}

	if (pclose(fp) != 0) {
		ret = EPKG_FATAL;
		goto done;
	}

	if (sbuf_data(*sig)[sbuf_len(*sig) -1 ] == '\n')
		sbuf_setpos(*sig, sbuf_len(*sig) -1);

	sbuf_finish(*sig);
	sbuf_finish(*cert);
done:
	if (cmd)
		sbuf_delete(cmd);

	return (ret);
}

static int
pkg_repo_pack_db(const char *name, const char *archive, char *path,
		struct rsa_key *rsa, char **argv, int argc)
{
	struct packing *pack;
	unsigned char *sigret = NULL;
	unsigned int siglen = 0;
	char fname[MAXPATHLEN];
	struct sbuf *sig, *pub;

	sig = NULL;
	pub = NULL;

	if (packing_init(&pack, archive, TXZ) != EPKG_OK)
		return (EPKG_FATAL);

	if (rsa != NULL) {
		if (rsa_sign(path, rsa, &sigret, &siglen) != EPKG_OK) {
			packing_finish(pack);
			unlink(path);
			return (EPKG_FATAL);
		}

		if (packing_append_buffer(pack, sigret, "signature", siglen + 1) != EPKG_OK) {
			free(sigret);
			free(pack);
			unlink(path);
			return (EPKG_FATAL);
		}

		free(sigret);
	} else if (argc >= 1) {
		if (pkg_repo_sign(path, argv, argc, &sig, &pub) != EPKG_OK) {
			packing_finish(pack);
			unlink(path);
			return (EPKG_FATAL);
		}

		snprintf(fname, sizeof(fname), "%s.sig", name);
		if (packing_append_buffer(pack, sbuf_data(sig), fname, sbuf_len(sig)) != EPKG_OK) {
			packing_finish(pack);
			sbuf_delete(sig);
			sbuf_delete(pub);
			unlink(path);
			return (EPKG_FATAL);
		}

		snprintf(fname, sizeof(fname), "%s.pub", name);
		if (packing_append_buffer(pack, sbuf_data(pub), fname, sbuf_len(pub)) != EPKG_OK) {
			packing_finish(pack);
			unlink(path);
			sbuf_delete(sig);
			sbuf_delete(pub);
			return (EPKG_FATAL);
		}

	}
	packing_append_file_attr(pack, path, name, "root", "wheel", 0644);

	packing_finish(pack);
	unlink(path);
	if (sig != NULL)
		sbuf_delete(sig);
	if (pub != NULL)
		sbuf_delete(pub);

	return (EPKG_OK);
}

int
pkg_finish_repo(const char *output_dir, pem_password_cb *password_cb,
    char **argv, int argc, bool filelist)
{
	char repo_path[MAXPATHLEN];
	char repo_archive[MAXPATHLEN];
	struct rsa_key *rsa = NULL;
	struct stat st;
	int ret = EPKG_OK;

	if (!is_dir(output_dir)) {
		pkg_emit_error("%s is not a directory", output_dir);
		return (EPKG_FATAL);
	}

	if (argc == 1) {
		rsa_new(&rsa, password_cb, argv[0]);
	}

	if (argc > 1 && strcmp(argv[0], "signing_command:") != 0)
		return (EPKG_FATAL);

	if (argc > 1) {
		argc--;
		argv++;
	}

	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
	    repo_packagesite_file);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", output_dir,
	    repo_packagesite_archive);
	if (pkg_repo_pack_db(repo_packagesite_file, repo_archive, repo_path, rsa, argv, argc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	if (filelist) {
		snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
		    repo_filesite_file);
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s",
		    output_dir, repo_filesite_archive);
		if (pkg_repo_pack_db(repo_filesite_file, repo_archive, repo_path, rsa, argv, argc) != EPKG_OK) {
			ret = EPKG_FATAL;
			goto cleanup;
		}
	}

	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
	    repo_digests_file);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", output_dir,
	    repo_digests_archive);
	if (pkg_repo_pack_db(repo_digests_file, repo_archive, repo_path, rsa, argv, argc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}
	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
		repo_conflicts_file);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", output_dir,
		repo_conflicts_archive);
	if (pkg_repo_pack_db(repo_conflicts_file, repo_archive, repo_path, rsa, argv, argc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	/* Now we need to set the equal mtime for all archives in the repo */
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz",
	    output_dir, repo_db_archive);
	if (stat(repo_archive, &st) == 0) {
		struct timeval ftimes[2] = {
			{
			.tv_sec = st.st_mtime,
			.tv_usec = 0
			},
			{
			.tv_sec = st.st_mtime,
			.tv_usec = 0
			}
		};
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz",
		    output_dir, repo_packagesite_archive);
		utimes(repo_archive, ftimes);
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz",
		    output_dir, repo_digests_archive);
		utimes(repo_archive, ftimes);
		if (filelist) {
			snprintf(repo_archive, sizeof(repo_archive),
			    "%s/%s.txz", output_dir, repo_filesite_archive);
			utimes(repo_archive, ftimes);
		}
	}

cleanup:
	if (rsa)
		rsa_free(rsa);

	return (ret);
}
