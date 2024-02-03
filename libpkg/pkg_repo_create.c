/*-
 * Copyright (c) 2011-2024 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
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
#include <sys/file.h>
#include <sys/time.h>

#include <archive_entry.h>
#include <assert.h>
#include <fts.h>
#include <libgen.h>
#include <sqlite3.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <fcntl.h>
#include <dirent.h>

#include "tllist.h"
#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

enum {
	MSG_PKG_DONE=0,
	MSG_PKG_READY,
};

static int pkg_repo_pack_db(const char *name, const char *archive, char *path,
    struct pkg_key *keyinfo, struct pkg_repo_create *prc);

static int
hash_file(struct pkg_repo_meta *meta, struct pkg *pkg, char *path)
{
	char tmp_repo[MAXPATHLEN] = { 0 };
	char tmp_name[MAXPATHLEN] = { 0 };
	char repo_name[MAXPATHLEN] = { 0 };
	char hash_name[MAXPATHLEN] = { 0 };
	char link_name[MAXPATHLEN] = { 0 };
	char *rel_repo = NULL;
	char *rel_dir = NULL;
	char *rel_link = NULL;
	char *ext = NULL;

	/* Don't rename symlinks */
	if (is_link(path))
		return (EPKG_OK);

	ext = strrchr(path, '.');

	strlcpy(tmp_name, path, sizeof(tmp_name));
	rel_dir = get_dirname(tmp_name);
	while (strstr(rel_dir, "/Hashed") != NULL) {
		rel_dir = get_dirname(rel_dir);
	}
	strlcpy(tmp_name, rel_dir, sizeof(tmp_name));
	rel_dir = (char *)&tmp_name;

	rel_repo = path;
	if (strncmp(rel_repo, meta->repopath, strlen(meta->repopath)) == 0) {
		rel_repo += strlen(meta->repopath);
		while (rel_repo[0] == '/')
			rel_repo++;
	}
	strlcpy(tmp_repo, rel_repo, sizeof(tmp_repo));
	rel_repo = get_dirname(tmp_repo);
	while (strstr(rel_repo, "/Hashed") != NULL) {
		rel_repo = get_dirname(rel_repo);
	}
	strlcpy(tmp_repo, rel_repo, sizeof(tmp_repo));
	rel_repo = (char *)&tmp_repo;

	pkg_snprintf(repo_name, sizeof(repo_name), "%S/%S/%n-%v%S%z%S",
	    rel_repo, PKG_HASH_DIR, pkg, pkg, PKG_HASH_SEPSTR, pkg, ext);
	pkg_snprintf(link_name, sizeof(repo_name), "%S/%n-%v%S",
	    rel_dir, pkg, pkg, ext);
	pkg_snprintf(hash_name, sizeof(hash_name), "%S/%S/%n-%v%S%z%S",
	    rel_dir, PKG_HASH_DIR, pkg, pkg, PKG_HASH_SEPSTR, pkg, ext);
	rel_link = (char *)&hash_name;
	rel_link += strlen(rel_dir);
	while (rel_link[0] == '/')
		rel_link++;

	snprintf(tmp_name, sizeof(tmp_name), "%s/%s", rel_dir, PKG_HASH_DIR);
	rel_dir = (char *)&tmp_name;
	if (!is_dir(rel_dir)) {
		pkg_debug(1, "Making directory: %s", rel_dir);
		(void)pkg_mkdirs(rel_dir);
	}

	if (strcmp(path, hash_name) != 0) {
		pkg_debug(1, "Rename the pkg from: %s to: %s", path, hash_name);
		if (rename(path, hash_name) == -1) {
			pkg_emit_errno("rename", hash_name);
			return (EPKG_FATAL);
		}
	}
	if (meta->hash_symlink) {
		pkg_debug(1, "Symlinking pkg file from: %s to: %s", rel_link,
		    link_name);
		(void)unlink(link_name);
		if (symlink(rel_link, link_name) == -1) {
			pkg_emit_errno("symlink", link_name);
			return (EPKG_FATAL);
		}
	}
	free(pkg->repopath);
	pkg->repopath = xstrdup(repo_name);

	return (EPKG_OK);
}

struct pkg_fts_item {
	char *fts_accpath;
	char *pkg_path;
	char *fts_name;
	off_t fts_size;
	int fts_info;
};
typedef tll(struct pkg_fts_item *) fts_item_t;

static struct pkg_fts_item*
pkg_create_repo_fts_new(FTSENT *fts, const char *root_path)
{
	struct pkg_fts_item *item;
	char *pkg_path;

	item = xmalloc(sizeof(*item));
	item->fts_accpath = xstrdup(fts->fts_accpath);
	item->fts_name = xstrdup(fts->fts_name);
	item->fts_size = fts->fts_statp->st_size;
	item->fts_info = fts->fts_info;

	pkg_path = fts->fts_path;
	pkg_path += strlen(root_path);
	while (pkg_path[0] == '/')
		pkg_path++;

	item->pkg_path = xstrdup(pkg_path);

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
pkg_create_repo_read_fts(fts_item_t *items, FTS *fts,
	const char *repopath, size_t *plen, struct pkg_repo_meta *meta)
{
	FTSENT *fts_ent;
	struct pkg_fts_item *fts_cur;
	char *ext;
	int linklen = 0;
	char tmp_name[MAXPATHLEN] = { 0 };
	char repo_path[MAXPATHLEN];
	size_t repo_path_len;

	if (realpath(repopath, repo_path) == NULL) {
		pkg_emit_errno("invalid repo path", repopath);
		return (EPKG_FATAL);
	}
	repo_path_len = strlen(repo_path);
	errno = 0;

	while ((fts_ent = fts_read(fts)) != NULL) {
		/*
		 * Skip directories starting with '.' to avoid Poudriere
		 * symlinks.
		 */
		if ((fts_ent->fts_info == FTS_D ||
		    fts_ent->fts_info == FTS_DP) &&
		    fts_ent->fts_namelen > 2 &&
		    fts_ent->fts_name[0] == '.') {
			fts_set(fts, fts_ent, FTS_SKIP);
			continue;
		}
		/*
		 * Ignore 'Latest' directory as it is just symlinks back to
		 * already-processed packages.
		 */
		if ((fts_ent->fts_info == FTS_D ||
		    fts_ent->fts_info == FTS_DP ||
		    fts_ent->fts_info == FTS_SL) &&
		    strcmp(fts_ent->fts_name, "Latest") == 0) {
			fts_set(fts, fts_ent, FTS_SKIP);
			continue;
		}
		/* Follow symlinks. */
		if (fts_ent->fts_info == FTS_SL) {
			/*
			 * Skip symlinks pointing inside the repo
			 * and dead symlinks
			 */
			if (realpath(fts_ent->fts_path, tmp_name) == NULL)
				continue;
			if (strncmp(repo_path, tmp_name, repo_path_len) == 0)
				continue;
			/* Skip symlinks to hashed packages */
			if (meta->hash) {
				linklen = readlink(fts_ent->fts_path,
				    (char *)&tmp_name, MAXPATHLEN);
				if (linklen < 0)
					continue;
				tmp_name[linklen] = '\0';
				if (strstr(tmp_name, PKG_HASH_DIR) != NULL)
					continue;
			}
			fts_set(fts, fts_ent, FTS_FOLLOW);
			/* Restart. Next entry will be the resolved file. */
			continue;
		}
		/* Skip everything that is not a file */
		if (fts_ent->fts_info != FTS_F)
			continue;

		ext = strrchr(fts_ent->fts_name, '.');

		if (ext == NULL)
			continue;

		if (!packing_is_valid_format(ext + 1))
			continue;

		/* skip all files which are not .pkg */
		if (!ctx.repo_accept_legacy_pkg && strcmp(ext + 1, "pkg") != 0)
			continue;


		*ext = '\0';

		if (pkg_repo_meta_is_old_file(fts_ent->fts_name, meta)) {
			unlink(fts_ent->fts_path);
			continue;
		}
		if (strcmp(fts_ent->fts_name, "meta") == 0 ||
				pkg_repo_meta_is_special_file(fts_ent->fts_name, meta)) {
			*ext = '.';
			continue;
		}

		*ext = '.';
		fts_cur = pkg_create_repo_fts_new(fts_ent, repopath);
		if (fts_cur == NULL)
			return (EPKG_FATAL);

		tll_push_front(*items, fts_cur);
		(*plen) ++;
	}

	if (errno != 0) {
		pkg_emit_errno("fts_read", "pkg_create_repo_read_fts");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

struct thr_env {
	int ntask;
	int ffd;
	int mfd;
	int dfd;
	struct ucl_emitter_context *ctx;
	struct pkg_repo_meta *meta;
	fts_item_t fts_items;
	pthread_mutex_t nlock;
	pthread_mutex_t llock;
	pthread_mutex_t flock;
	pthread_cond_t cond;
};

static void *
pkg_create_repo_thread(void *arg)
{
	struct thr_env *te = (struct thr_env *)arg;
	int flags, ret = EPKG_OK;
	struct pkg *pkg = NULL;
	char *path;
	const char *repopath;
	struct pkg_fts_item *items = NULL;

	pkg_debug(1, "start worker to parse packages");

	if (te->ffd != -1)
		flags = PKG_OPEN_MANIFEST_ONLY;
	else
		flags = PKG_OPEN_MANIFEST_ONLY | PKG_OPEN_MANIFEST_COMPACT;

	for (;;) {
		if (items != NULL)
			pkg_create_repo_fts_free(items);
		pthread_mutex_lock(&te->llock);
		if (tll_length(te->fts_items) == 0) {
			pthread_mutex_unlock(&te->llock);
			goto cleanup;
		}
		items = tll_pop_front(te->fts_items);
		pthread_mutex_unlock(&te->llock);
		path = items->fts_accpath;
		repopath = items->pkg_path;
		if (pkg_open(&pkg, path, flags) == EPKG_OK) {
			struct stat st;

			pkg->sum = pkg_checksum_file(path, PKG_HASH_TYPE_SHA256_HEX);
			stat(path, &st);
			pkg->pkgsize = st.st_size;
			if (te->meta->hash) {
				ret = hash_file(te->meta, pkg, path);
				if (ret != EPKG_OK)
					goto cleanup;
			} else {
				pkg->repopath = xstrdup(repopath);
			}

			/*
			 * TODO: use pkg_checksum for new manifests
			 */
			pthread_mutex_lock(&te->flock);
			ucl_object_t *o = pkg_emit_object(pkg, 0);
			ucl_object_emit_streamline_add_object(te->ctx, o);
			ucl_object_emit_fd(o, UCL_EMIT_JSON_COMPACT, te->mfd);
			dprintf(te->mfd, "\n");
			fdatasync(te->mfd);
			ucl_object_unref(o);

			pthread_mutex_unlock(&te->flock);

			if (te->ffd != -1) {
				FILE *fl;

				if (flock(te->ffd, LOCK_EX) == -1) {
					pkg_emit_errno("pkg_create_repo_worker", "flock");
					ret = EPKG_FATAL;
					goto cleanup;
				}
				fl = fdopen(dup(te->ffd), "a");
				pkg_emit_filelist(pkg, fl);
				fclose(fl);

				flock(te->ffd, LOCK_UN);
			}
			pkg_free(pkg);
		}
		pthread_mutex_lock(&te->nlock);
		te->ntask++;
		pthread_cond_signal(&te->cond);
		pthread_mutex_unlock(&te->nlock);
	}

cleanup:
	pkg_debug(1, "worker done");
	return (NULL);
}

#ifdef __linux__
typedef const FTSENT *FTSENTP;
#else
typedef const FTSENT *const FTSENTP;
#endif

static int
fts_compare(FTSENTP *a, FTSENTP *b)
{
	/* Sort files before directories, then alpha order */
	if ((*a)->fts_info != FTS_D && (*b)->fts_info == FTS_D)
		return -1;
	if ((*a)->fts_info == FTS_D && (*b)->fts_info != FTS_D)
		return 1;
	return (strcmp((*a)->fts_name, (*b)->fts_name));
}

struct pkg_repo_create *
pkg_repo_create_new(void)
{
	struct pkg_repo_create *prc;

	prc = xcalloc(1, sizeof(*prc));
	prc->ofd = -1;

	return (prc);
}

void
pkg_repo_create_free(struct pkg_repo_create *prc)
{
	if (prc == NULL)
		return;
	pkg_repo_meta_free(prc->meta);
	if (prc->ofd != -1)
		close(prc->ofd);
	ucl_object_unref(prc->groups);
	free(prc);
}

static void
group_load(struct pkg_repo_create *prc, int dfd, const char *name, ucl_object_t *schema)
{
	struct ucl_parser *p;
	ucl_object_t *obj = NULL;
	int fd;
	struct ucl_schema_error err;

	fd = openat(dfd, name, O_RDONLY);
	if (fd == -1) {
		pkg_emit_error("Unable to open group: %s", name);
		return;
	}

	p = ucl_parser_new(0);
	if (!ucl_parser_add_fd(p, fd)) {
		pkg_emit_error("Error parsing group '%s': %s'",
		    name, ucl_parser_get_error(p));
		ucl_parser_free(p);
		close(fd);
		return;
	}
	close(fd);

	obj = ucl_parser_get_object(p);
	ucl_parser_free(p);
	if (obj == NULL)
		return;

	if (!ucl_object_validate(schema, obj, &err)) {
		pkg_emit_error("group definition %s cannot be validated: %s",
		    name, err.msg);
		ucl_object_unref(obj);
		return;
	}
	if (prc->groups == NULL)
		prc->groups = ucl_object_typed_new(UCL_ARRAY);
	ucl_array_append(prc->groups, obj);
}

static ucl_object_t *
group_open_schema(void)
{
	struct ucl_parser *parser;
	ucl_object_t *group_schema;
	static const char group_schema_str[] = ""
		"{"
		"  type = object;"
		"  properties: {"
		"    name: { type = string };"
		"    requires: { "
		"      type = array;"
		"      item = { type = string };"
		"    };"
		"    depends: { "
		"      type = array;"
		"      item = { type = string };"
		"    };"
		"    comment: { type = string };"
		"  };"
		"  required = [ name, comment ];"
		"};";
	parser = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_chunk(parser, group_schema_str,
	    sizeof(group_schema_str) -1)) {
		pkg_emit_error("Cannot parse schema for group: %s",
		    ucl_parser_get_error(parser));
		    ucl_parser_free(parser);
		    return (NULL);
	}
	group_schema = ucl_parser_get_object(parser);
	ucl_parser_free(parser);
	return (group_schema);

}

void
pkg_repo_create_set_groups(struct pkg_repo_create *prc, const char *path)
{
	int dfd = open(path, O_DIRECTORY);
	DIR *d;
	struct dirent *e;
	struct stat st;
	ucl_object_t *schema;

	if (dfd == -1) {
		pkg_emit_error("Unable to open the groups directory '%s'", path);
		return;
	}

	d = fdopendir(dfd);
	if (d == NULL) {
		pkg_emit_error("Unable to open the groups directory '%s'", path);
		close(dfd);
		return;
	}

	schema = group_open_schema();

	while ((e = readdir(d)) != NULL) {
		const char *ext;
		/* ignore all hidden files */
		if (e->d_name[0] == '.')
			continue;
		/* only consider files ending with .ucl */
		ext = strrchr(e->d_name, '.');
		if (ext == NULL)
			continue;
		if (strcmp(ext, ".ucl") != 0)
			continue;
		/* only regular files are considered */
		if (fstatat(dfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			pkg_emit_errno("fstatat", e->d_name);
			return;
		}
		if (!S_ISREG(st.st_mode))
			continue;
		group_load(prc, dfd, e->d_name, schema);
	}
	closedir(d);
	ucl_object_unref(schema);
}

void
pkg_repo_create_set_create_filelist(struct pkg_repo_create *prc, bool val)
{
	prc->filelist = val;
}

void
pkg_repo_create_set_hash(struct pkg_repo_create *prc, bool val)
{
	prc->hash = val;
}

void
pkg_repo_create_set_hash_symlink(struct pkg_repo_create *prc, bool val)
{
	prc->hash_symlink = val;
}

void
pkg_repo_create_set_output_dir(struct pkg_repo_create *prc, const char *out)
{
	prc->outdir = out;
}

void
pkg_repo_create_set_metafile(struct pkg_repo_create *prc, const char *metafile)
{
	prc->metafile = metafile;
}

void
pkg_repo_create_set_sign(struct pkg_repo_create *prc, char **argv, int argc, pkg_password_cb *cb)
{
	prc->sign.argc = argc;
	prc->sign.argv = argv;
	prc->sign.cb = cb;
}

static int
pkg_repo_create_pack_and_sign(struct pkg_repo_create *prc)
{
	char repo_path[MAXPATHLEN];
	char repo_archive[MAXPATHLEN];
	char *key_file;
	const char *key_type;
	struct pkg_key *keyinfo = NULL;
	struct stat st;
	int ret = EPKG_OK, nfile = 0;
	const int files_to_pack = 4;

	if (prc->sign.argc == 1) {
		key_type = key_file = prc->sign.argv[0];
		if (strncmp(key_file, "rsa:", 4) == 0) {
			key_file += 4;
			*(key_file - 1) = '\0';
		} else {
			key_type = "rsa";
		}

		pkg_debug(1, "Loading %s key from '%s' for signing", key_type, key_file);
		rsa_new(&keyinfo, prc->sign.cb, key_file);
	}

	if (prc->sign.argc > 1 && strcmp(prc->sign.argv[0], "signing_command:") != 0)
		return (EPKG_FATAL);

	if (prc->sign.argc > 1) {
		prc->sign.argc--;
		prc->sign.argv++;
	}

	pkg_emit_progress_start("Packing files for repository");
	pkg_emit_progress_tick(nfile++, files_to_pack);

	snprintf(repo_path, sizeof(repo_path), "%s/%s", prc->outdir,
	    prc->meta->manifests);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", prc->outdir,
		prc->meta->manifests_archive);
	if (pkg_repo_pack_db(prc->meta->manifests, repo_archive, repo_path, keyinfo, prc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	pkg_emit_progress_tick(nfile++, files_to_pack);

	if (prc->filelist) {
		snprintf(repo_path, sizeof(repo_path), "%s/%s", prc->outdir,
		    prc->meta->filesite);
		snprintf(repo_archive, sizeof(repo_archive), "%s/%s",
		    prc->outdir, prc->meta->filesite_archive);
		if (pkg_repo_pack_db(prc->meta->filesite, repo_archive, repo_path, keyinfo, prc) != EPKG_OK) {
			ret = EPKG_FATAL;
			goto cleanup;
		}
	}

	pkg_emit_progress_tick(nfile++, files_to_pack);
	snprintf(repo_path, sizeof(repo_path), "%s/%s", prc->outdir, prc->meta->data);
	snprintf(repo_archive, sizeof(repo_archive), "%s/%s", prc->outdir,
	    prc->meta->data_archive);
	if (pkg_repo_pack_db(prc->meta->data, repo_archive, repo_path, keyinfo, prc) != EPKG_OK) {
		ret = EPKG_FATAL;
		goto cleanup;
	}

	pkg_emit_progress_tick(nfile++, files_to_pack);

	if (fstatat(prc->ofd, "meta.conf", &st, 0) == 0) {
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
		snprintf(repo_archive, sizeof(repo_archive), "%s.pkg",
		    prc->meta->manifests_archive);
		futimesat(prc->ofd, repo_archive, ftimes);
		if (prc->filelist) {
			snprintf(repo_archive, sizeof(repo_archive),
			    "%s.pkg", prc->meta->filesite_archive);
			futimesat(prc->ofd, repo_archive, ftimes);
		}
		snprintf(repo_archive, sizeof(repo_archive), "%s.pkg",
		    prc->meta->data_archive);
		futimesat(prc->ofd, repo_archive, ftimes);
	}

cleanup:
	pkg_emit_progress_tick(files_to_pack, files_to_pack);

	rsa_free(keyinfo);

	return (ret);
}

int
pkg_repo_create(struct pkg_repo_create *prc, char *path)
{
	FTS *fts = NULL;
	int num_workers;
	pthread_t *threads;
	struct thr_env te = { 0 };
	size_t len;
	int fd;
	int retcode = EPKG_FATAL;
	ucl_object_t *meta_dump;
	char *repopath[2];

	if (prc->outdir == NULL)
		prc->outdir = path;

	te.mfd = te.ffd = te.dfd = -1;

	if (!is_dir(path)) {
		pkg_emit_error("%s is not a directory", path);
		return (EPKG_FATAL);
	}

	errno = 0;
	if (!is_dir(prc->outdir)) {
		/* Try to create dir */
		if (errno == ENOENT) {
			if (mkdir(prc->outdir, 00755) == -1) {
				pkg_fatal_errno("cannot create output directory %s",
					prc->outdir);
			}
		}
		else {
			pkg_emit_error("%s is not a directory", prc->outdir);
			return (EPKG_FATAL);
		}
	}
	if ((prc->ofd = open(prc->outdir, O_DIRECTORY)) == -1) {
		pkg_emit_error("Cannot open %s", prc->outdir);
		return (EPKG_FATAL);
	}

	if (prc->metafile != NULL) {
		fd = open(prc->metafile, O_RDONLY);
		if (fd == -1) {
			pkg_emit_error("meta loading error while trying %s", prc->metafile);
			return (EPKG_FATAL);
		}
		if (pkg_repo_meta_load(fd, &prc->meta) != EPKG_OK) {
			pkg_emit_error("meta loading error while trying %s", prc->metafile);
			close(fd);
			return (EPKG_FATAL);
		}
		close(fd);
	} else {
		prc->meta = pkg_repo_meta_default();
	}
	prc->meta->repopath = path;
	prc->meta->hash = prc->hash;
	prc->meta->hash_symlink = prc->hash_symlink;

	te.meta = prc->meta;

	repopath[0] = path;
	repopath[1] = NULL;

	num_workers = pkg_object_int(pkg_config_get("WORKERS_COUNT"));
	if (num_workers <= 0) {
		num_workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
		if (num_workers == -1)
			num_workers = 6;
	}

	if ((fts = fts_open(repopath, FTS_PHYSICAL|FTS_NOCHDIR, fts_compare)) == NULL) {
		pkg_emit_errno("fts_open", path);
		goto cleanup;
	}

	if ((te.mfd = openat(prc->ofd, prc->meta->manifests,
	     O_CREAT|O_TRUNC|O_WRONLY, 00644)) == -1) {
		goto cleanup;
	}
	if ((te.dfd = openat(prc->ofd, prc->meta->data,
	    O_CREAT|O_TRUNC|O_WRONLY, 00644)) == -1) {
		goto cleanup;
	}
	if (prc->filelist) {
		if ((te.ffd = openat(prc->ofd, prc->meta->filesite,
		        O_CREAT|O_TRUNC|O_WRONLY, 00644)) == -1) {
			goto cleanup;
		}
	}

	len = 0;

	pkg_create_repo_read_fts(&te.fts_items, fts, path, &len, prc->meta);

	if (len == 0) {
		/* Nothing to do */
		pkg_emit_error("No package files have been found");
		goto cleanup;
	}

	/* Split items over all workers */
	num_workers = MIN(num_workers, len);

	/* Launch workers */
	pkg_emit_progress_start("Creating repository in %s", prc->outdir);

	threads = xcalloc(num_workers, sizeof(pthread_t));

	struct ucl_emitter_functions *f;
	ucl_object_t *obj = ucl_object_typed_new(UCL_OBJECT);
	/*
	 * Work around a bug in the streamline exporter which creates an invalid
	 * json if there is nothing in the object, prior to the streamline to
	 * start. So always add at least an empty groups array
	 */
	ucl_object_insert_key(obj,
	    prc->groups == NULL ? ucl_object_typed_new(UCL_ARRAY) : prc->groups,
	    "groups", 0, false);
	f = ucl_object_emit_fd_funcs(te.dfd);
	te.ctx = ucl_object_emit_streamline_new(obj, UCL_EMIT_JSON_COMPACT, f);
	ucl_object_t *ar = ucl_object_typed_new(UCL_ARRAY);
	ar->key = "packages";
	ar->keylen = sizeof("packages") -1;

	ucl_object_emit_streamline_start_container(te.ctx, ar);

	for (int i = 0; i < num_workers; i++) {
		/* Create new worker */
		pthread_create(&threads[i], NULL, &pkg_create_repo_thread, &te);
	}

	pthread_mutex_lock(&te.nlock);
	while (te.ntask < len) {
		pthread_cond_wait(&te.cond, &te.nlock);
		pkg_emit_progress_tick(te.ntask, len);
	}
	pthread_mutex_unlock(&te.nlock);

	for (int i = 0; i < num_workers; i++)
		pthread_join(threads[i], NULL);
	ucl_object_emit_streamline_end_container(te.ctx);
	pkg_emit_progress_tick(len, len);
	ucl_object_emit_streamline_finish(te.ctx);
	ucl_object_emit_funcs_free(f);
	ucl_object_unref(obj);

	/* Write metafile */

	fd = openat(prc->ofd, "meta", O_CREAT|O_TRUNC|O_CLOEXEC|O_WRONLY,
	    0644);
	if (fd != -1) {
		meta_dump = pkg_repo_meta_to_ucl(prc->meta);
		ucl_object_emit_fd(meta_dump, UCL_EMIT_CONFIG, fd);
		close(fd);
		fd = openat(prc->ofd, "meta.conf",
		    O_CREAT|O_TRUNC|O_CLOEXEC|O_WRONLY, 0644);
		if (fd != -1) {
			ucl_object_emit_fd(meta_dump, UCL_EMIT_CONFIG, fd);
			close(fd);;
		} else {
			pkg_emit_notice("cannot create metafile at 'meta.conf'");
		}
		ucl_object_unref(meta_dump);
	}
	else {
		pkg_emit_notice("cannot create metafile at 'meta'");
	}
	retcode = EPKG_OK;
cleanup:
	if (te.mfd != -1)
		close(te.mfd);
	if (te.ffd != -1)
		close(te.ffd);
	if (te.dfd != -1)
		close(te.dfd);
	if (fts != NULL)
		fts_close(fts);

	tll_free_and_free(te.fts_items, pkg_create_repo_fts_free);

	if (retcode != EPKG_OK)
		return (retcode);

	return (pkg_repo_create_pack_and_sign(prc));
}

static int
pkg_repo_sign(const char *path, char **argv, int argc, char **sig, size_t *siglen,
    char **cert)
{
	FILE *fp;
	char *sha256;
	xstring *cmd = NULL;
	xstring *buf = NULL;
	xstring *sigstr = NULL;
	xstring *certstr = NULL;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int i, ret = EPKG_OK;

	sha256 = pkg_checksum_file(path, PKG_HASH_TYPE_SHA256_HEX);
	if (!sha256)
		return (EPKG_FATAL);

	cmd = xstring_new();

	for (i = 0; i < argc; i++) {
		if (strspn(argv[i], " \t\n") > 0)
			fprintf(cmd->fp, " \"%s\" ", argv[i]);
		else
			fprintf(cmd->fp, " %s ", argv[i]);
	}

	fflush(cmd->fp);
	if ((fp = popen(cmd->buf, "r+")) == NULL) {
		ret = EPKG_FATAL;
		goto done;
	}

	fprintf(fp, "%s\n", sha256);

	sigstr = xstring_new();
	certstr = xstring_new();

	while ((linelen = getline(&line, &linecap, fp)) > 0 ) {
		if (strcmp(line, "SIGNATURE\n") == 0) {
			buf = sigstr;
			continue;
		} else if (strcmp(line, "CERT\n") == 0) {
			buf = certstr;
			continue;
		} else if (strcmp(line, "END\n") == 0) {
			break;
		}
		if (buf != NULL) {
			fwrite(line, linelen, 1, buf->fp);
		}
	}

	*cert = xstring_get(certstr);
	fclose(sigstr->fp);
	sigstr->size--;
	*siglen = sigstr->size;
	*sig = sigstr->buf;
	free(sigstr);

	/* remove the latest \n */

	if (pclose(fp) != 0) {
		ret = EPKG_FATAL;
		goto done;
	}

done:
	free(sha256);
	xstring_free(cmd);

	return (ret);
}

static int
pack_rsa_sign(struct packing *pack, struct pkg_key *keyinfo, const char *path,
    const char *name)
{
	unsigned char *sigret = NULL;
	unsigned int siglen = 0;

	if (keyinfo == NULL)
		return (EPKG_FATAL);

	if (rsa_sign(path, keyinfo, &sigret, &siglen) != EPKG_OK) {
		free(sigret);
		return (EPKG_FATAL);
	}
	if (packing_append_buffer(pack, sigret, name, siglen + 1) != EPKG_OK) {
		free(sigret);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

static int
pack_command_sign(struct packing *pack, const char *path, char **argv, int argc,
    const char *name)
{
	size_t signature_len = 0;
	char fname[MAXPATHLEN];
	char *sig, *pub;

	sig = NULL;
	pub = NULL;

	if (pkg_repo_sign(path, argv, argc, &sig, &signature_len, &pub) != EPKG_OK) {
		free(sig);
		free(pub);
		return (EPKG_FATAL);
	}

	snprintf(fname, sizeof(fname), "%s.sig", name);
	if (packing_append_buffer(pack, sig, fname, signature_len) != EPKG_OK) {
		free(sig);
		free(pub);
		return (EPKG_FATAL);
	}
	free(sig);

	snprintf(fname, sizeof(fname), "%s.pub", name);
	if (packing_append_buffer(pack, pub, fname, strlen(pub)) != EPKG_OK) {
		free(pub);
		return (EPKG_FATAL);
	}
	free(pub);

	return (EPKG_OK);
}

static int
pkg_repo_pack_db(const char *name, const char *archive, char *path,
    struct pkg_key *keyinfo, struct pkg_repo_create *prc)
{
	struct packing *pack;
	int ret = EPKG_OK;

	if (packing_init(&pack, archive, prc->meta->packing_format, 0, (time_t)-1, true, true) != EPKG_OK)
		return (EPKG_FATAL);

	if (keyinfo != NULL) {
		ret = pack_rsa_sign(pack, keyinfo, path, "signature");
	} else if (prc->sign.argc >= 1) {
		ret = pack_command_sign(pack, path, prc->sign.argv, prc->sign.argc, name);
	}
	packing_append_file_attr(pack, path, name, "root", "wheel", 0644, 0);

	packing_finish(pack);
	unlink(path);

	return (ret);
}
