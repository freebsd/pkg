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
#include "pkg_config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/time.h>

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
#include <sys/uio.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"


struct digest_list_entry {
	char *origin;
	char *digest;
	long manifest_pos;
	long files_pos;
	long manifest_length;
	char *checksum;
	struct digest_list_entry *prev, *next;
};

struct pkg_conflict_bulk {
	struct pkg_conflict *conflicts;
	char *file;
	UT_hash_handle hh;
};

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
	new->uid = strdup(uniqueid);

	HASH_ADD_KEYPTR(hh, bulk->conflicts, new->uid, strlen(new->uid), new);
}

static void
pkg_repo_write_conflicts (struct pkg_conflict_bulk *bulk, FILE *out)
{
	struct pkg_conflict_bulk	*pkg_bulk = NULL, *cur, *tmp, *s;
	struct pkg_conflict	*c1, *c1tmp, *c2, *c2tmp, *ctmp;

	/*
	 * Here we reorder bulk hash from hash by file
	 * to hash indexed by a package, so we iterate over the
	 * original hash and create a new hash indexed by package name
	 */

	HASH_ITER (hh, bulk, cur, tmp) {
		HASH_ITER (hh, cur->conflicts, c1, c1tmp) {
			HASH_FIND_STR(pkg_bulk, c1->uid, s);
			if (s == NULL) {
				/* New entry required */
				s = malloc(sizeof(struct pkg_conflict_bulk));
				if (s == NULL) {
					pkg_emit_errno("malloc", "struct pkg_conflict_bulk");
					goto out;
				}
				memset(s, 0, sizeof(struct pkg_conflict_bulk));
				s->file = c1->uid;
				HASH_ADD_KEYPTR(hh, pkg_bulk, s->file, strlen(s->file), s);
			}
			/* Now add all new entries from this file to this conflict structure */
			HASH_ITER (hh, cur->conflicts, c2, c2tmp) {
				if (strcmp(c1->uid, c2->uid) == 0)
					continue;

				HASH_FIND_STR(s->conflicts, c2->uid, ctmp);
				if (ctmp == NULL)
					pkg_repo_new_conflict(c2->uid, s);
			}
		}
	}

	HASH_ITER (hh, pkg_bulk, cur, tmp) {
		fprintf(out, "%s:", cur->file);
		HASH_ITER (hh, cur->conflicts, c1, c1tmp) {
			if (c1->hh.next != NULL)
				fprintf(out, "%s,", c1->uid);
			else
				fprintf(out, "%s\n", c1->uid);
		}
	}
out:
	HASH_ITER (hh, pkg_bulk, cur, tmp) {
		HASH_ITER (hh, cur->conflicts, c1, c1tmp) {
			HASH_DEL(cur->conflicts, c1);
			free(c1->uid);
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
	const char *repopath, size_t *plen, struct pkg_repo_meta *meta)
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

		if (strcmp(ext + 1, packing_format_to_string(meta->packing_format)) != 0)
			continue;

		*ext = '\0';

		if (strcmp(fts_ent->fts_name, "meta") == 0 ||
				pkg_repo_meta_is_special_file(fts_ent->fts_name, meta)) {
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
	const char *mlfile, const char *flfile, int pip,
	struct pkg_repo_meta *meta)
{
	pid_t pid;
	int mfd, ffd = -1;
	bool read_files = (flfile != NULL);
	bool legacy = (meta == NULL);
	int flags, ret = EPKG_OK;
	size_t cur_job = 0;
	struct pkg_fts_item *cur;
	struct pkg *pkg = NULL;
	struct pkg_manifest_key *keys = NULL;
	char checksum[SHA256_DIGEST_LENGTH * 3 + 1], *mdigest = NULL;
	char digestbuf[1024];
	struct iovec iov[2];
	struct msghdr msg;

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

	pkg_manifest_keys_new(&keys);
	pkg_debug(1, "start worker to parse %d packages", nelts);

	if (read_files)
		flags = PKG_OPEN_MANIFEST_ONLY;
	else
		flags = PKG_OPEN_MANIFEST_ONLY | PKG_OPEN_MANIFEST_COMPACT;

	if (read(pip, digestbuf, 1) == -1) {
		pkg_emit_errno("pkg_create_repo_worker", "read");
		goto cleanup;
	}

	LL_FOREACH(start, cur) {
		if (cur_job >= nelts)
			break;

		if (pkg_open(&pkg, cur->fts_accpath, keys, flags) == EPKG_OK) {
			int r;
			off_t mpos, fpos = 0;
			size_t mlen;

			sha256_file(cur->fts_accpath, checksum);
			pkg->pkgsize = cur->fts_size;
			pkg->sum = strdup(checksum);
			pkg->repopath = strdup(cur->pkg_path);

			/*
			 * TODO: use pkg_checksum for new manifests
			 */
			sbuf_clear(b);
			if (legacy)
				pkg_emit_manifest_sbuf(pkg, b, PKG_MANIFEST_EMIT_COMPACT, &mdigest);
			else {
				mdigest = malloc(pkg_checksum_type_size(meta->digest_format));

				pkg_emit_manifest_sbuf(pkg, b, PKG_MANIFEST_EMIT_COMPACT, NULL);
				if (pkg_checksum_generate(pkg, mdigest,
				     pkg_checksum_type_size(meta->digest_format),
				     meta->digest_format) != EPKG_OK) {
					pkg_emit_error("Cannot generate digest for a package");
					ret = EPKG_FATAL;

					goto cleanup;
				}
			}
			mlen = sbuf_len(b);
			sbuf_finish(b);

			if (flock(mfd, LOCK_EX) == -1) {
				pkg_emit_errno("pkg_create_repo_worker", "flock");
				ret = EPKG_FATAL;
				goto cleanup;
			}

			mpos = lseek(mfd, 0, SEEK_END);

			iov[0].iov_base = sbuf_data(b);
			iov[0].iov_len = sbuf_len(b);
			iov[1].iov_base = (void *)"\n";
			iov[1].iov_len = 1;

			if (writev(mfd, iov, 2) == -1) {
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
				fl = fdopen(dup(ffd), "a");
				pkg_emit_filelist(pkg, fl);
				fclose(fl);

				flock(ffd, LOCK_UN);
			}

			r = snprintf(digestbuf, sizeof(digestbuf), "%s:%s:%ld:%ld:%ld:%s\n",
				pkg->origin,
				mdigest,
				(long)mpos,
				(long)fpos,
				(long)mlen,
				pkg->sum);

			free(mdigest);
			mdigest = NULL;
			iov[0].iov_base = digestbuf;
			iov[0].iov_len = r;
			memset(&msg, 0, sizeof(msg));
			msg.msg_iov = iov;
			msg.msg_iovlen = 1;
			sendmsg(pip, &msg, MSG_EOR);
		}
		cur_job ++;
	}

cleanup:
	pkg_manifest_keys_free(keys);

	write(pip, ".\n", 2);
	close(pip);
	close(mfd);
	if (read_files)
		close(ffd);
	free(mdigest);

	pkg_debug(1, "worker done");
	exit(ret);
}

static int
pkg_create_repo_read_pipe(int fd, struct digest_list_entry **dlist)
{
	struct digest_list_entry *dig = NULL;
	char buf[1024];
	int r, i, start;
	enum {
		s_set_origin = 0,
		s_set_digest,
		s_set_mpos,
		s_set_fpos,
		s_set_mlen,
		s_set_checksum
	} state = 0;

	for (;;) {
		r = read(fd, buf, sizeof(buf));

		if (r == -1) {
			if (errno == EINTR)
				continue;
			else if (errno == ECONNRESET) {
				/* Treat it as the end of a connection */
				return (EPKG_END);
			}
			else if (errno == EAGAIN || errno == EWOULDBLOCK)
				return (EPKG_OK);

			pkg_emit_errno("pkg_create_repo_read_pipe", "read");
			return (EPKG_FATAL);
		}
		else if (r == 0)
			return (EPKG_END);

		/*
		 * XXX: can parse merely full lines
		 */
		start = 0;
		for (i = 0; i < r; i ++) {
			if (buf[i] == ':') {
				switch(state) {
				case s_set_origin:
					dig = calloc(1, sizeof(*dig));
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
					dig->manifest_length = strtol(&buf[start], NULL, 10);
					state = s_set_checksum;
					break;
				case s_set_checksum:
					dig->checksum =  malloc(i - start + 1);
					strlcpy(dig->digest, &buf[start], i - start + 1);
					state = s_set_origin;
					break;
				}
				start = i + 1;
			}
			else if (buf[i] == '\n') {
				if (state == s_set_mlen) {
					dig->manifest_length = strtol(&buf[start], NULL, 10);
				}
				else if (state == s_set_checksum) {
					dig->checksum =  malloc(i - start + 1);
					strlcpy(dig->checksum, &buf[start], i - start + 1);
				}
				assert(dig->origin != NULL);
				assert(dig->digest != NULL);
				DL_APPEND(*dlist, dig);
				state = s_set_origin;
				start = i + 1;
				break;
			}
			else if (buf[i] == '.' && buf[i + 1] == '\n') {
				return (EPKG_END);
			}
		}
	}

	/*
	 * Never reached
	 */
	return (EPKG_OK);
}

int
pkg_create_repo(char *path, const char *output_dir, bool filelist,
	const char *metafile, bool legacy)
{
	FTS *fts = NULL;
	struct pkg_fts_item *fts_items = NULL, *fts_cur, *fts_start;

	struct pkg_conflict *c, *ctmp;
	struct pkg_conflict_bulk *conflicts = NULL, *curcb, *tmpcb;
	int num_workers, i, remaining_workers, remain, cur_jobs, remain_jobs, nworker;
	size_t len, tasks_per_worker, ntask;
	struct digest_list_entry *dlist = NULL, *cur_dig, *dtmp;
	struct pollfd *pfd = NULL;
	int cur_pipe[2], fd;
	struct pkg_repo_meta *meta;
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

	if (metafile != NULL) {
		if (pkg_repo_meta_load(metafile, &meta) != EPKG_OK) {
			pkg_emit_error("meta loading error while trying %s", metafile);
			return (EPKG_FATAL);
		}
	}
	else {
		meta = pkg_repo_meta_default();
	}

	repopath[0] = path;
	repopath[1] = NULL;

	num_workers = pkg_object_int(pkg_config_get("WORKERS_COUNT"));
	if (num_workers <= 0) {
		len = sizeof(num_workers);
#ifdef HAVE_SYSCTLBYNAME
		if (sysctlbyname("hw.ncpu", &num_workers, &len, NULL, 0) == -1)
			num_workers = 6;
#else
		num_workers = 6;
#endif
	}

	if ((fts = fts_open(repopath, FTS_PHYSICAL|FTS_NOCHDIR, NULL)) == NULL) {
		pkg_emit_errno("fts_open", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	snprintf(packagesite, sizeof(packagesite), "%s/%s", output_dir,
	    meta->manifests);
	if ((fd = open(packagesite, O_CREAT|O_TRUNC|O_WRONLY, 00644)) == -1) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}
	close(fd);
	if (filelist) {
		snprintf(filesite, sizeof(filesite), "%s/%s", output_dir,
		    meta->filesite);
		if ((fd = open(filesite, O_CREAT|O_TRUNC|O_WRONLY, 00644)) == -1) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
		close(fd);
	}
	snprintf(repodb, sizeof(repodb), "%s/%s", output_dir,
	    meta->digests);
	if ((mandigests = fopen(repodb, "w")) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	len = 0;

	pkg_create_repo_read_fts(&fts_items, fts, path, &len, meta);

	if (len == 0) {
		/* Nothing to do */
		pkg_emit_error("No package files have been found");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	/* Split items over all workers */
	num_workers = MIN(num_workers, len);
	tasks_per_worker = len / num_workers;
	/* How much extra tasks should be distributed over the workers */
	remain = len % num_workers;
	assert(tasks_per_worker > 0);

	/* Launch workers */
	pkg_emit_progress_start("Creating repository in %s", output_dir);

	pfd = calloc(num_workers, sizeof(struct pollfd));
	ntask = 0;
	cur_jobs = (remain > 0) ? tasks_per_worker + 1 : tasks_per_worker;
	remain_jobs = cur_jobs;
	fts_start = fts_items;
	nworker = 0;

	LL_FOREACH(fts_items, fts_cur) {
		if (--remain_jobs == 0) {
			/* Create new worker */
			int ofl;
			int st = SOCK_DGRAM;

#ifdef HAVE_SEQPACKET
			st = SOCK_SEQPACKET;
#endif
			if (socketpair(AF_UNIX, st, 0, cur_pipe) == -1) {
				pkg_emit_errno("pkg_create_repo", "pipe");
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (pkg_create_repo_worker(fts_start, cur_jobs,
					packagesite, (filelist ? filesite : NULL), cur_pipe[1],
					(legacy ? NULL : meta)) == EPKG_FATAL) {
				close(cur_pipe[0]);
				close(cur_pipe[1]);
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			pfd[nworker].fd = cur_pipe[0];
			pfd[nworker].events = POLLIN;
			close(cur_pipe[1]);
			/* Make our end of the pipe non-blocking */
			ofl = fcntl(cur_pipe[0], F_GETFL, 0);
			fcntl(cur_pipe[0], F_SETFL, ofl | O_NONBLOCK);

			if (--remain > 0)
				cur_jobs = tasks_per_worker + 1;
			else
				cur_jobs = tasks_per_worker;

			remain_jobs = cur_jobs;
			fts_start = fts_cur->next;
			nworker ++;
		}
		ntask ++;
	}

	/* Send start marker to all workers */
	for (i = 0; i < num_workers; i ++) {
		if (write(pfd[i].fd, ".", 1) == -1)
			pkg_emit_errno("pkg_create_repo", "write");
	}

	ntask = 0;
	remaining_workers = num_workers;
	while(remaining_workers > 0) {
		int st;

		pkg_debug(1, "checking for %d workers", remaining_workers);
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
				if (pfd[i].fd != -1 &&
								(pfd[i].revents & (POLLIN|POLLHUP|POLLERR))) {
					if (pkg_create_repo_read_pipe(pfd[i].fd, &dlist) != EPKG_OK) {
						/*
						 * Wait for the worker finished
						 */

						while (wait(&st) == -1) {
							if (errno == EINTR)
								continue;

							pkg_emit_errno("pkg_create_repo", "wait");
							break;
						}

						remaining_workers --;
						pkg_debug(1, "finished worker, %d remaining",
							remaining_workers);
						pfd[i].events = 0;
						pfd[i].revents = 0;
						close(pfd[i].fd);
						pfd[i].fd = -1;
					}
					else {
						pkg_emit_progress_tick(ntask++, len);
					}
				}
			}
		}
	}

	pkg_emit_progress_tick(len, len);
	retcode = EPKG_OK;

	/* Now sort all digests */
	DL_SORT(dlist, pkg_digest_sort_compare_func);

	/*
	 * XXX: it is not used actually
	 */
#if 0
	pkg_repo_write_conflicts(conflicts, fconflicts);
#endif

	/* Write metafile */
	if (!legacy) {
		ucl_object_t *meta_dump;
		FILE *mfile;

		snprintf(repodb, sizeof(repodb), "%s/%s", output_dir,
			"meta");
		if ((mfile = fopen(repodb, "w")) != NULL) {
			meta_dump = pkg_repo_meta_to_ucl(meta);
			ucl_object_emit_file(meta_dump, UCL_EMIT_CONFIG, mfile);
			ucl_object_unref(meta_dump);
			fclose(mfile);
		}
		else {
			pkg_emit_notice("cannot create metafile at %s", repodb);
		}
	}
cleanup:
	HASH_ITER (hh, conflicts, curcb, tmpcb) {
		HASH_ITER (hh, curcb->conflicts, c, ctmp) {
			free(c->uid);
			HASH_DEL(curcb->conflicts, c);
			free(c);
		}
		HASH_DEL(conflicts, curcb);
		free(curcb);
	}

	if (pfd != NULL)
		free(pfd);
	if (fts != NULL)
		fts_close(fts);

	LL_FREE(fts_items, pkg_create_repo_fts_free);
	LL_FOREACH_SAFE(dlist, cur_dig, dtmp) {
		if (cur_dig->checksum != NULL)
			fprintf(mandigests, "%s:%s:%ld:%ld:%ld:%s\n", cur_dig->origin,
				cur_dig->digest, cur_dig->manifest_pos, cur_dig->files_pos,
				cur_dig->manifest_length, cur_dig->checksum);
		else
			fprintf(mandigests, "%s:%s:%ld:%ld:%ld\n", cur_dig->origin,
				cur_dig->digest, cur_dig->manifest_pos, cur_dig->files_pos,
				cur_dig->manifest_length);

		free(cur_dig->digest);
		free(cur_dig->origin);
		free(cur_dig);
	}

	pkg_repo_meta_free(meta);

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
	sbuf_finish(cmd);

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
		struct rsa_key *rsa, struct pkg_repo_meta *meta,
		char **argv, int argc)
{
	struct packing *pack;
	unsigned char *sigret = NULL;
	unsigned int siglen = 0;
	char fname[MAXPATHLEN];
	struct sbuf *sig, *pub;

	sig = NULL;
	pub = NULL;

	if (packing_init(&pack, archive, meta->packing_format, false) != EPKG_OK)
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
	struct pkg_repo_meta *meta;
	struct stat st;
	int ret = EPKG_OK, nfile = 0;
	const int files_to_pack = 4;
	bool legacy = false;

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

	pkg_emit_progress_start("Packing files for repository");
	pkg_emit_progress_tick(nfile++, files_to_pack);

	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
		repo_meta_file);
	/*
	 * If no meta is defined, then it is a legacy repo
	 */
	if (access(repo_path, R_OK) != -1) {
		if (pkg_repo_meta_load(repo_path, &meta) != EPKG_OK) {
			pkg_emit_error("meta loading error while trying %s", repo_path);
			return (EPKG_FATAL);
		}
		else {
			meta = pkg_repo_meta_default();
		}
		if (pkg_repo_pack_db(repo_meta_file, repo_path, repo_path, rsa, meta,
			argv, argc) != EPKG_OK) {
			ret = EPKG_FATAL;
			goto cleanup;
		}
	}
	else {
		legacy = true;
		meta = pkg_repo_meta_default();
	}

	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
	    meta->manifests);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", output_dir,
		meta->manifests_archive);
	if (pkg_repo_pack_db(meta->manifests, repo_archive, repo_path, rsa, meta,
		argv, argc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	pkg_emit_progress_tick(nfile++, files_to_pack);

	if (filelist) {
		snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
		    meta->filesite);
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s",
		    output_dir, meta->filesite_archive);
		if (pkg_repo_pack_db(meta->filesite, repo_archive, repo_path, rsa, meta,
			argv, argc) != EPKG_OK) {
			ret = EPKG_FATAL;
			goto cleanup;
		}
	}

	pkg_emit_progress_tick(nfile++, files_to_pack);

	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
	    meta->digests);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", output_dir,
	    meta->digests_archive);
	if (pkg_repo_pack_db(meta->digests, repo_archive, repo_path, rsa, meta,
		argv, argc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	pkg_emit_progress_tick(nfile++, files_to_pack);

#if 0
	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
		meta->conflicts);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", output_dir,
		meta->conflicts_archive);
	if (pkg_repo_pack_db(meta->conflicts, repo_archive, repo_path, rsa, meta,
		argv, argc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}
#endif

	/* Now we need to set the equal mtime for all archives in the repo */
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz",
	    output_dir, repo_meta_file);
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
		    output_dir, meta->manifests_archive);
		utimes(repo_archive, ftimes);
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz",
		    output_dir, meta->digests_archive);
		utimes(repo_archive, ftimes);
		if (filelist) {
			snprintf(repo_archive, sizeof(repo_archive),
			    "%s/%s.txz", output_dir, meta->filesite_archive);
			utimes(repo_archive, ftimes);
		}
		if (!legacy) {
			snprintf(repo_archive, sizeof(repo_archive),
				"%s/%s.txz", output_dir, repo_meta_file);
			utimes(repo_archive, ftimes);
		}
	}

cleanup:
	pkg_emit_progress_tick(files_to_pack, files_to_pack);
	pkg_repo_meta_free(meta);

	rsa_free(rsa);

	return (ret);
}
