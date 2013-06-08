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
#include <stdbool.h>
#include <unistd.h>

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
	char dest[MAXPATHLEN + 1];
	char url[MAXPATHLEN + 1];
	int fetched = 0;
	char cksum[SHA256_DIGEST_LENGTH * 2 +1];
	char *path = NULL;
	const char *packagesite = NULL;
	const char *cachedir = NULL;
	int retcode = EPKG_OK;
	const char *sum, *name, *version, *reponame;
	struct pkg_repo *repo;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE);

	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);

	pkg_get(pkg, PKG_REPONAME, &reponame,
	    PKG_CKSUM, &sum, PKG_NAME, &name, PKG_VERSION, &version);

	snprintf(dest, sizeof(dest), "%s/%s", cachedir, repopath);

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

static void
pack_extract(const char *pack, const char *dbname, const char *dbpath)
{
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;

	if (access(pack, F_OK) != 0)
		return;

	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_tar(a);
	if (archive_read_open_filename(a, pack, 4096) != ARCHIVE_OK) {
		/* if we can't unpack it it won't be useful for us */
		unlink(pack);
		archive_read_free(a);
		return;
	}

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), dbname) == 0) {
			archive_entry_set_pathname(ae, dbpath);
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
			break;
		}
	}

	archive_read_free(a);
}

struct digest_list_entry {
	char *origin;
	char *digest;
	long manifest_pos;
	long files_pos;
	struct digest_list_entry *next;
};

static int
digest_sort_compare_func(struct digest_list_entry *d1, struct digest_list_entry *d2)
{
	return strcmp(d1->origin, d2->origin);
}

int
pkg_create_repo(char *path, bool force, bool filelist,
		void (progress)(struct pkg *pkg, void *data), void *data)
{
	FTS *fts = NULL;
	struct thd_data thd_data;
	int num_workers;
	size_t len;
	pthread_t *tids = NULL;
	struct digest_list_entry *dlist = NULL, *cur_dig, *dtmp;
	sqlite3 *sqlite = NULL;

	char *errmsg = NULL;
	int retcode = EPKG_OK;

	char *repopath[2];
	char repodb[MAXPATHLEN + 1];
	char repopack[MAXPATHLEN + 1];
	char *manifest_digest;
	FILE *psyml, *fsyml, *mandigests;

	psyml = fsyml = mandigests = NULL;

	if (!is_dir(path)) {
		pkg_emit_error("%s is not a directory", path);
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

	snprintf(repodb, sizeof(repodb), "%s/%s", path, repo_packagesite_file);
	if ((psyml = fopen(repodb, "w")) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}
	if (filelist) {
		snprintf(repodb, sizeof(repodb), "%s/%s", path, repo_filesite_file);
		if ((fsyml = fopen(repodb, "w")) == NULL) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}
	snprintf(repodb, sizeof(repodb), "%s/%s", path, repo_digests_file);
	if ((mandigests = fopen(repodb, "w")) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	snprintf(repodb, sizeof(repodb), "%s/%s", path, repo_db_file);
	snprintf(repopack, sizeof(repopack), "%s/repoy.txz", path);

	pack_extract(repopack, repo_db_file, repodb);

	if ((retcode = pkgdb_repo_open(repodb, force, &sqlite, true)) != EPKG_OK)
		goto cleanup;

	if ((retcode = pkgdb_repo_init(sqlite, true)) != EPKG_OK)
		goto cleanup;

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

		long manifest_pos, files_pos;

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
			continue;
		}

		/* do not add if package if already in repodb
		   (possibly at a different pkg_path) */

		retcode = pkgdb_repo_cksum_exists(sqlite, r->cksum);
		if (retcode == EPKG_FATAL) {
			goto cleanup;
		}
		else if (retcode == EPKG_OK) {
			continue;
		}

		if (progress != NULL)
			progress(r->pkg, data);

		manifest_pos = ftell(psyml);
		pkg_emit_manifest_file(r->pkg, psyml, PKG_MANIFEST_EMIT_COMPACT, &manifest_digest);
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
		LL_PREPEND(dlist, cur_dig);

		retcode = pkgdb_repo_add_package(r->pkg, r->path, sqlite,
				manifest_digest, false, true);
		if (retcode == EPKG_END) {
			continue;
		}
		else if (retcode != EPKG_OK) {
			goto cleanup;
		}

		pkg_free(r->pkg);
		free(r);
	}

	/* Now sort all digests */
	LL_SORT(dlist, digest_sort_compare_func);
cleanup:
	if (pkgdb_repo_close(sqlite, retcode == EPKG_OK) != EPKG_OK) {
		retcode = EPKG_FATAL;
	}
	LL_FOREACH_SAFE(dlist, cur_dig, dtmp) {
		if (retcode == EPKG_OK) {
			fprintf(mandigests, "%s:%s:%ld:%ld\n", cur_dig->origin,
				cur_dig->digest, cur_dig->manifest_pos, cur_dig->files_pos);
		}
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

	if (mandigests != NULL)
		fclose(mandigests);

	if (sqlite != NULL)
		sqlite3_close(sqlite);

	if (errmsg != NULL)
		sqlite3_free(errmsg);

	sqlite3_shutdown();

	return (retcode);
}

void
read_pkg_file(void *data)
{
	struct thd_data *d = (struct thd_data*) data;
	struct pkg_result *r;
	struct pkg_manifest_key *keys = NULL;

	FTSENT *fts_ent = NULL;
	char fts_accpath[MAXPATHLEN + 1];
	char fts_path[MAXPATHLEN + 1];
	char fts_name[MAXPATHLEN + 1];
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
			strcmp(fts_name, repo_digests_archive) == 0)
			continue;
		*ext = '.';

		pkg_path = fts_path;
		pkg_path += strlen(d->root_path);
		while (pkg_path[0] == '/')
			pkg_path++;

		r = calloc(1, sizeof(struct pkg_result));

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
pack_db(const char *name, const char *archive, char *path, struct rsa_key *rsa)
{
	struct packing *pack;
	unsigned char *sigret = NULL;
	unsigned int siglen = 0;

	if (packing_init(&pack, archive, TXZ) != EPKG_OK)
		return (EPKG_FATAL);

	if (rsa != NULL) {
		if (rsa_sign(path, rsa, &sigret, &siglen) != EPKG_OK) {
			packing_finish(pack);
			return (EPKG_FATAL);
		}

		if (packing_append_buffer(pack, sigret, "signature", siglen + 1) != EPKG_OK) {
			free(sigret);
			return (EPKG_FATAL);
		}

		free(sigret);
	}
	packing_append_file_attr(pack, path, name, "root", "wheel", 0644);

	unlink(path);
	packing_finish(pack);

	return (EPKG_OK);
}

int
pkg_finish_repo(char *path, pem_password_cb *password_cb, char *rsa_key_path, bool filelist)
{
	char repo_path[MAXPATHLEN + 1];
	char repo_archive[MAXPATHLEN + 1];
	struct rsa_key *rsa = NULL;
	struct stat st;
	
	if (!is_dir(path)) {
	    pkg_emit_error("%s is not a directory", path);
	    return (EPKG_FATAL);
	}

	if (rsa_key_path != NULL)
		rsa_new(&rsa, password_cb, rsa_key_path);

	snprintf(repo_path, sizeof(repo_path), "%s/%s", path, repo_packagesite_file);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", path, repo_packagesite_archive);
	if (pack_db(repo_packagesite_file, repo_archive, repo_path, rsa) != EPKG_OK)
		return (EPKG_FATAL);

	snprintf(repo_path, sizeof(repo_path), "%s/%s", path, repo_db_file);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", path, repo_db_archive);
	if (pack_db(repo_db_file, repo_archive, repo_path, rsa) != EPKG_OK)
		return (EPKG_FATAL);

	if (filelist) {
		snprintf(repo_path, sizeof(repo_path), "%s/%s", path, repo_filesite_file);
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s", path, repo_filesite_archive);
		if (pack_db(repo_filesite_file, repo_archive, repo_path, rsa) != EPKG_OK)
			return (EPKG_FATAL);
	}

	snprintf(repo_path, sizeof(repo_path), "%s/%s", path, repo_digests_file);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", path, repo_digests_archive);
	if (pack_db(repo_digests_file, repo_archive, repo_path, rsa) != EPKG_OK)
		return (EPKG_FATAL);

	/* Now we need to set the equal mtime for all archives in the repo */
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz", path, repo_db_archive);
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
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz", path, repo_packagesite_archive);
		utimes(repo_archive, ftimes);
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz", path, repo_digests_archive);
		utimes(repo_archive, ftimes);
		if (filelist) {
			snprintf(repo_archive, sizeof(repo_archive), "%s/%s.txz", path, repo_filesite_archive);
			utimes(repo_archive, ftimes);
		}
	}

	rsa_free(rsa);

	return (EPKG_OK);
}
