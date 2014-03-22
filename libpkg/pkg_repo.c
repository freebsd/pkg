/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
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

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/repodb.h"
#include "private/thd_repo.h"

int
pkg_repo_fetch(struct pkg *pkg)
{
	char dest[MAXPATHLEN];
	char url[MAXPATHLEN];
	int fetched = 0;
	char cksum[SHA256_DIGEST_LENGTH * 2 +1];
	char *path = NULL;
	const char *packagesite = NULL;
	const char *cachedir = NULL;
	int retcode = EPKG_OK;
	const char *sum, *name, *version, *reponame;
	struct pkg_repo *repo;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE);

	cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));

	pkg_get(pkg, PKG_REPONAME, &reponame,
	    PKG_CKSUM, &sum, PKG_NAME, &name, PKG_VERSION, &version);

	pkg_snprintf(dest, sizeof(dest), "%S/%R", cachedir, pkg);

	/* If it is already in the local cachedir, dont bother to
	 * download it */
	if (access(dest, F_OK) == 0)
		goto checksum;

	/* Create the dirs in cachedir */
	if ((path = dirname(dest)) == NULL) {
		pkg_emit_errno("dirname", dest);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if ((retcode = mkdirs(path)) != EPKG_OK)
		goto cleanup;

	/*
	 * In multi-repos the remote URL is stored in pkg[PKG_REPOURL]
	 * For a single attached database the repository URL should be
	 * defined by PACKAGESITE.
	 */
	repo = pkg_repo_find_name(reponame);
	packagesite = pkg_repo_url(repo);

	if (packagesite == NULL || packagesite[0] == '\0') {
		pkg_emit_error("PACKAGESITE is not defined");
		retcode = 1;
		goto cleanup;
	}

	if (packagesite[strlen(packagesite) - 1] == '/')
		pkg_snprintf(url, sizeof(url), "%S%R", packagesite, pkg);
	else
		pkg_snprintf(url, sizeof(url), "%S/%R", packagesite, pkg);

	retcode = pkg_fetch_file(repo, url, dest, 0);
	fetched = 1;

	if (retcode != EPKG_OK)
		goto cleanup;

	checksum:
	retcode = sha256_file(dest, cksum);
	if (retcode == EPKG_OK)
		if (strcmp(cksum, sum)) {
			if (fetched == 1) {
				pkg_emit_error("%s-%s failed checksum "
				    "from repository", name, version);
				retcode = EPKG_FATAL;
			} else {
				pkg_emit_error("cached package %s-%s: "
				    "checksum mismatch, fetching from remote",
				    name, version);
				unlink(dest);
				return (pkg_repo_fetch(pkg));
			}
		}

	cleanup:
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

struct digest_list_entry {
	char *origin;
	char *digest;
	long manifest_pos;
	long files_pos;
	long manifest_length;
	struct digest_list_entry *next;
};

struct pkg_conflict_bulk {
	struct pkg_conflict *conflicts;
	char *file;
	UT_hash_handle hh;
};

static int
digest_sort_compare_func(struct digest_list_entry *d1, struct digest_list_entry *d2)
{
	return strcmp(d1->origin, d2->origin);
}

static void
pkg_repo_new_conflict(const char *origin, struct pkg_conflict_bulk *bulk)
{
	struct pkg_conflict *new;

	pkg_conflict_new(&new);
	sbuf_set(&new->origin, origin);

	HASH_ADD_KEYPTR(hh, bulk->conflicts,
			__DECONST(char *, pkg_conflict_origin(new)),
			sbuf_size(new->origin), new);
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
			HASH_FIND_STR(pkg_bulk, sbuf_get(c1->origin), s);
			if (s == NULL) {
				/* New entry required */
				s = malloc(sizeof(struct pkg_conflict_bulk));
				if (s == NULL) {
					pkg_emit_errno("malloc", "struct pkg_conflict_bulk");
					goto out;
				}
				memset(s, 0, sizeof(struct pkg_conflict_bulk));
				s->file = sbuf_get(c1->origin);
				HASH_ADD_KEYPTR(hh, pkg_bulk, s->file, strlen(s->file), s);
			}
			/* Now add all new entries from this file to this conflict structure */
			HASH_ITER (hh, cur->conflicts, c2, c2tmp) {
				new = true;
				if (strcmp(sbuf_get(c1->origin), sbuf_get(c2->origin)) == 0)
					continue;

				HASH_FIND_STR(s->conflicts, sbuf_get(c2->origin), ctmp);
				if (ctmp == NULL)
					pkg_repo_new_conflict(sbuf_get(c2->origin), s);
			}
		}
	}

	HASH_ITER (hh, pkg_bulk, cur, tmp) {
		fprintf(out, "%s:", cur->file);
		HASH_ITER (hh, cur->conflicts, c1, c1tmp) {
			if (c1->hh.next != NULL)
				fprintf(out, "%s,", sbuf_get(c1->origin));
			else
				fprintf(out, "%s\n", sbuf_get(c1->origin));
		}
	}
out:
	HASH_ITER (hh, pkg_bulk, cur, tmp) {
		HASH_ITER (hh, cur->conflicts, c1, c1tmp) {
			HASH_DEL(cur->conflicts, c1);
			sbuf_free(c1->origin);
			free(c1);
		}
		HASH_DEL(pkg_bulk, cur);
		free(cur);
	}
	return;
}

int
pkg_create_repo(char *path, const char *output_dir, bool filelist,
		void (progress)(struct pkg *pkg, void *data), void *data)
{
	FTS *fts = NULL;
	struct thd_data thd_data;
	struct pkg_conflict *c, *ctmp;
	struct pkg_conflict_bulk *conflicts = NULL, *curcb, *tmpcb;
	int num_workers;
	size_t len;
	pthread_t *tids = NULL;
	struct digest_list_entry *dlist = NULL, *cur_dig, *dtmp;

	int retcode = EPKG_OK;

	char *repopath[2];
	char repodb[MAXPATHLEN];
	char *manifest_digest;
	FILE *psyml, *fsyml, *mandigests, *fconflicts;

	psyml = fsyml = mandigests = fconflicts = NULL;

	if (!is_dir(path)) {
		pkg_emit_error("%s is not a directory", path);
		return (EPKG_FATAL);
	}

	if (!is_dir(output_dir)) {
		pkg_emit_error("%s is not a directory", output_dir);
		return (EPKG_FATAL);
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

	snprintf(repodb, sizeof(repodb), "%s/%s", output_dir,
	    repo_packagesite_file);
	if ((psyml = fopen(repodb, "w")) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}
	if (filelist) {
		snprintf(repodb, sizeof(repodb), "%s/%s", output_dir,
		    repo_filesite_file);
		if ((fsyml = fopen(repodb, "w")) == NULL) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}
	snprintf(repodb, sizeof(repodb), "%s/%s", output_dir,
	    repo_digests_file);
	if ((mandigests = fopen(repodb, "w")) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	snprintf(repodb, sizeof(repodb), "%s/%s", output_dir, repo_conflicts_file);
	if ((fconflicts = fopen(repodb, "w")) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	thd_data.root_path = path;
	thd_data.max_results = num_workers;
	thd_data.num_results = 0;
	thd_data.stop = false;
	thd_data.fts = fts;
	thd_data.read_files = filelist;
	pthread_mutex_init(&thd_data.fts_m, NULL);
	thd_data.results = NULL;
	thd_data.thd_finished = 0;
	pthread_mutex_init(&thd_data.results_m, NULL);
	pthread_cond_init(&thd_data.has_result, NULL);
	pthread_cond_init(&thd_data.has_room, NULL);

	/* Launch workers */
	tids = calloc(num_workers, sizeof(pthread_t));
	for (int i = 0; i < num_workers; i++) {
		pthread_create(&tids[i], NULL, (void *)&read_pkg_file, &thd_data);
	}

	for (;;) {
		struct pkg_result *r;
		const char *origin;

		long manifest_pos, files_pos, manifest_length;

		pthread_mutex_lock(&thd_data.results_m);
		while ((r = thd_data.results) == NULL) {
			if (thd_data.thd_finished == num_workers) {
				break;
			}
			pthread_cond_wait(&thd_data.has_result, &thd_data.results_m);
		}
		if (r != NULL) {
			LL_DELETE(thd_data.results, thd_data.results);
			thd_data.num_results--;
			pthread_cond_signal(&thd_data.has_room);
		}
		pthread_mutex_unlock(&thd_data.results_m);
		if (r == NULL) {
			break;
		}

		if (r->retcode != EPKG_OK) {
			free(r);
			continue;
		}

		/* EPKG_END returned */

		if (progress != NULL)
			progress(r->pkg, data);

		manifest_pos = ftell(psyml);
		pkg_emit_manifest_file(r->pkg, psyml, PKG_MANIFEST_EMIT_COMPACT, &manifest_digest);
		manifest_length = ftell(psyml) - manifest_pos;
		if (filelist) {
			files_pos = ftell(fsyml);
			pkg_emit_filelist(r->pkg, fsyml);
		} else {
			files_pos = 0;
		}

		pkg_get(r->pkg, PKG_ORIGIN, &origin);

		cur_dig = malloc(sizeof (struct digest_list_entry));
		cur_dig->origin = strdup(origin);
		cur_dig->digest = manifest_digest;
		cur_dig->manifest_pos = manifest_pos;
		cur_dig->files_pos = files_pos;
		cur_dig->manifest_length = manifest_length;
		LL_PREPEND(dlist, cur_dig);

		pkg_free(r->pkg);
		free(r);
	}

	/* Now sort all digests */
	LL_SORT(dlist, digest_sort_compare_func);

	pkg_repo_write_conflicts(conflicts, fconflicts);
cleanup:
	HASH_ITER (hh, conflicts, curcb, tmpcb) {
		HASH_ITER (hh, curcb->conflicts, c, ctmp) {
			sbuf_free(c->origin);
			HASH_DEL(curcb->conflicts, c);
			free(c);
		}
		HASH_DEL(conflicts, curcb);
		free(curcb);
	}
	LL_FOREACH_SAFE(dlist, cur_dig, dtmp) {
		fprintf(mandigests, "%s:%s:%ld:%ld:%ld\n", cur_dig->origin,
		    cur_dig->digest, cur_dig->manifest_pos, cur_dig->files_pos,
		    cur_dig->manifest_length);
		free(cur_dig->digest);
		free(cur_dig->origin);
		free(cur_dig);
	}
	if (tids != NULL) {
		// Cancel running threads
		if (retcode != EPKG_OK) {
			pthread_mutex_lock(&thd_data.fts_m);
			thd_data.stop = true;
			pthread_mutex_unlock(&thd_data.fts_m);
		}
		// Join on threads to release thread IDs
		for (int i = 0; i < num_workers; i++) {
			pthread_join(tids[i], NULL);
		}
		free(tids);
	}

	if (fts != NULL)
		fts_close(fts);

	if (fsyml != NULL)
		fclose(fsyml);

	if (psyml != NULL)
		fclose(psyml);

	if (fconflicts != NULL)
		fclose(fconflicts);

	if (mandigests != NULL)
		fclose(mandigests);

	return (retcode);
}

void
read_pkg_file(void *data)
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

		// There is no more jobs, exit the main loop.
		if (fts_ent == NULL)
			break;

		/* skip everything that is not a file */
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
		LL_APPEND(d->results, r);
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
cmd_sign(char *path, char **argv, int argc, struct sbuf **sig, struct sbuf **cert)
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
pack_db(const char *name, const char *archive, char *path, struct rsa_key *rsa, char **argv, int argc)
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
		if (cmd_sign(path, argv, argc, &sig, &pub) != EPKG_OK) {
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
	if (pack_db(repo_packagesite_file, repo_archive, repo_path, rsa, argv, argc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	if (filelist) {
		snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
		    repo_filesite_file);
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s",
		    output_dir, repo_filesite_archive);
		if (pack_db(repo_filesite_file, repo_archive, repo_path, rsa, argv, argc) != EPKG_OK) {
			ret = EPKG_FATAL;
			goto cleanup;
		}
	}

	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir,
	    repo_digests_file);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", output_dir,
	    repo_digests_archive);
	if (pack_db(repo_digests_file, repo_archive, repo_path, rsa, argv, argc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}
	snprintf(repo_path, sizeof(repo_path), "%s/%s", output_dir, 
		repo_conflicts_file);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", output_dir,
		repo_conflicts_archive);
	if (pack_db(repo_conflicts_file, repo_archive, repo_path, rsa, argv, argc) != EPKG_OK) {
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
