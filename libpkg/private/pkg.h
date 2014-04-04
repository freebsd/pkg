/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2013 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

#include <sys/param.h>
#include <sys/sbuf.h>
#include <sys/types.h>

#include <archive.h>
#include <sqlite3.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <stdbool.h>
#include <uthash.h>
#include <utlist.h>
#include <ucl.h>

#include "private/utils.h"

#define UCL_COUNT(obj) ((obj)?((obj)->len):0)

#define PKG_NUM_SCRIPTS 9

#if ARCHIVE_VERSION_NUMBER < 3000002
#define archive_write_add_filter_xz(a) archive_write_set_compression_xz(a)
#define archive_write_add_filter_bzip2(a) archive_write_set_compression_bzip2(a)
#define archive_write_add_filter_gzip(a) archive_write_set_compression_gzip(a)
#define archive_write_add_filter_none(a) archive_write_set_compression_none(a)
#define archive_read_support_filter_all(a) archive_read_support_compression_all(a)
#define archive_read_support_filter_none(a) archive_read_support_compression_none(a)
#endif

#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM | \
		ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_ACL | \
		ARCHIVE_EXTRACT_FFLAGS|ARCHIVE_EXTRACT_XATTR)

#define HASH_FREE(data, free_func) do {      \
	__typeof(data) hf1, hf2;                    \
	HASH_ITER(hh, data, hf1, hf2) {            \
		HASH_DEL(data, hf1);               \
		free_func(hf1);                    \
	}                                          \
	data = NULL;                               \
} while (0)

#define LL_FREE(head, free_func) do {   \
	__typeof(head) l1, l2;                 \
	LL_FOREACH_SAFE(head, l1, l2) {       \
		LL_DELETE(head, l1);          \
		free_func(l1);                \
	}                                     \
	head = NULL;                          \
} while (0)

#define HASH_NEXT(hash, data) do {            \
		if (data == NULL)             \
			data = hash;          \
		else                          \
			data = data->hh.next; \
		if (data == NULL)             \
			return (EPKG_END);    \
		else                          \
			return (EPKG_OK);     \
	} while (0)

#define HASH_FIND_UCLT(head,type,out)                            \
	HASH_FIND(hh, head, type, sizeof(enum ucl_type), out)
#define HASH_ADD_UCLT(head,type,add)                             \
	HASH_ADD(hh, head, type, sizeof(enum ucl_type), add)

#define HASH_FIND_YAMLT(head,type,out)                                   \
	HASH_FIND(hh,head,type,sizeof(yaml_node_type_t),out)
#define HASH_ADD_YAMLT(head,type,add)                                    \
	HASH_ADD(hh,head,type,sizeof(yaml_node_type_t),add)

#define HASH_FIND_YAMLEVT(head,type,out)                                   \
	HASH_FIND(hh,head,type,sizeof(yaml_event_type_t),out)
#define HASH_ADD_YAMLEVT(head,type,add)                                    \
	HASH_ADD(hh,head,type,sizeof(yaml_event_type_t),add)

extern int eventpipe;

struct pkg {
	struct sbuf	*fields[PKG_NUM_FIELDS];
	bool		 direct;
	bool		 automatic;
	bool		 locked;
	int64_t		 flatsize;
	int64_t		 old_flatsize;
	int64_t		 pkgsize;
	struct sbuf	*scripts[PKG_NUM_SCRIPTS];
	ucl_object_t		*licenses;
	ucl_object_t		*categories;
	struct pkg_dep		*deps;
	struct pkg_dep		*rdeps;
	struct pkg_file		*files;
	struct pkg_dir		*dirs;
	struct pkg_option	*options;
	struct pkg_user		*users;
	struct pkg_group	*groups;
	struct pkg_shlib	*shlibs_required;
	struct pkg_shlib	*shlibs_provided;
	ucl_object_t		*annotations;
	struct pkg_conflict *conflicts;
	struct pkg_provide	*provides;
	unsigned       	 flags;
	int64_t		 rowid;
	int64_t		 time;
	lic_t		 licenselogic;
	pkg_t		 type;
	UT_hash_handle	 hh;
	struct pkg	*next;
};

struct pkg_dep {
	struct sbuf	*origin;
	struct sbuf	*name;
	struct sbuf	*version;
	bool		 locked;
	UT_hash_handle	 hh;
};

enum pkg_conflict_type {
	PKG_CONFLICT_ALL = 0,
	PKG_CONFLICT_REMOTE_LOCAL,
	PKG_CONFLICT_REMOTE_REMOTE,
	PKG_CONFLICT_LOCAL_LOCAL
};

struct pkg_conflict {
	struct sbuf		*origin;
	enum pkg_conflict_type type;
	UT_hash_handle	hh;
};

struct pkg_provide {
	struct sbuf		*provide;
	UT_hash_handle	hh;
};

struct pkg_file {
	char		 path[MAXPATHLEN];
	int64_t		 size;
	char		 sum[SHA256_DIGEST_LENGTH * 2 + 1];
	char		 uname[MAXLOGNAME];
	char		 gname[MAXLOGNAME];
	bool		 keep;
	mode_t		 perm;
	UT_hash_handle	 hh;
};

struct pkg_dir {
	char		 path[MAXPATHLEN];
	char		 uname[MAXLOGNAME];
	char		 gname[MAXLOGNAME];
	mode_t		 perm;
	bool		 keep;
	bool		 try;
	UT_hash_handle	 hh;
};

struct pkg_option {
	struct sbuf	*key;
	struct sbuf	*value;
	struct sbuf	*default_value;
	struct sbuf	*description;
	UT_hash_handle	hh;
};

struct pkg_job_universe_item {
	struct pkg *pkg;
	struct job_pattern *jp;
	int priority;
	UT_hash_handle hh;
	struct pkg_job_universe_item *next, *prev;
};

struct pkg_job_request {
	struct pkg_job_universe_item *item;
	bool skip;
	UT_hash_handle hh;
};

struct pkg_solved {
	struct pkg_job_universe_item *items[2];
	pkg_solved_t type;
	bool already_deleted;
	struct pkg_solved *prev, *next;
};

struct pkg_job_seen {
	struct pkg_job_universe_item *un;
	const char *digest;
	UT_hash_handle hh;
};

struct pkg_job_provide {
	struct pkg_job_universe_item *un;
	const char *provide;
	struct pkg_job_provide *next, *prev;
	UT_hash_handle hh;
};

struct pkg_jobs {
	struct pkg_job_universe_item *universe;
	struct pkg_job_request	*request_add;
	struct pkg_job_request	*request_delete;
	struct pkg_solved *jobs;
	struct pkg_job_seen *seen;
	struct pkgdb	*db;
	struct pkg_job_provide *provides;
	pkg_jobs_t	 type;
	pkg_flags	 flags;
	int		 solved;
	int count;
	int total;
	int conflicts_registered;
	const char *	 reponame;
	struct job_pattern *patterns;
};

struct job_pattern {
	char		*pattern;
	char		*path;
	match_t		match;
	bool		is_file;
	UT_hash_handle hh;
};

struct pkg_user {
	char		 name[MAXLOGNAME];
	char		 uidstr[8192];/* taken from pw_util.c */
	UT_hash_handle	hh;
};

struct pkg_group {
	char		 name[MAXLOGNAME];
	char		 gidstr[8192]; /* taken from gw_util.c */
	UT_hash_handle	hh;
};

struct pkg_shlib {
	struct sbuf	*name;
	UT_hash_handle	hh;
};

struct http_mirror {
	struct url *url;
	struct http_mirror *next;
};

struct pkg_repo {
	repo_t type;
	char *name;
	char *url;
	char *pubkey;
	mirror_t mirror_type;
	union {
		struct dns_srvinfo *srv;
		struct http_mirror *http;
	};
	signature_t signature_type;
	char *fingerprints;
	FILE *ssh;

	struct {
		int in;
		int out;
		pid_t pid;
	} sshio;

	int (*update)(struct pkg_repo *, bool);

	bool enable;
	UT_hash_handle hh;
};

/* sql helpers */

typedef struct _sql_prstmt {
	sqlite3_stmt	*stmt;
	const char	*sql;
	const char	*argtypes;
} sql_prstmt;

#define STMT(x) (sql_prepared_statements[(x)].stmt)
#define SQL(x)  (sql_prepared_statements[(x)].sql)

/**
 * rc script actions
 */
typedef enum {
	PKG_RC_START = 0,
	PKG_RC_STOP
} pkg_rc_attr;

/**
 * Remove and unregister the package.
 * @param pkg An installed package to delete
 * @param db An opened pkgdb
 * @param force If set to one, the function will not fail if the package is
 * required by other packages.
 * @return An error code.
 */
int pkg_delete(struct pkg *pkg, struct pkgdb *db, unsigned flags);
#define PKG_DELETE_FORCE (1<<0)
#define PKG_DELETE_UPGRADE (1<<1)
#define PKG_DELETE_NOSCRIPT (1<<2)
#define PKG_DELETE_CONFLICT (1<<3)

int pkg_fetch_file_to_fd(struct pkg_repo *repo, const char *url, int dest, time_t *t);
int pkg_repo_fetch_package(struct pkg *pkg);
FILE * pkg_repo_fetch_remote_extract_tmp(struct pkg_repo *repo, const char *filename,
		const char *extension, time_t *t, int *rc, const char *archive_file);

typedef enum {
	HASH_UNKNOWN,
	HASH_SHA256,
} hash_t;

struct fingerprint {
	hash_t type;
	char hash[BUFSIZ];
	UT_hash_handle hh;
};
int pkg_repo_load_fingerprints(const char *path, struct fingerprint **f);


int pkg_start_stop_rc_scripts(struct pkg *, pkg_rc_attr attr);

int pkg_script_run(struct pkg *, pkg_script type);

int pkg_add_user_group(struct pkg *pkg);
int pkg_delete_user_group(struct pkgdb *db, struct pkg *pkg);

int pkg_open2(struct pkg **p, struct archive **a, struct archive_entry **ae,
	      const char *path, struct pkg_manifest_key *keys, int flags, int fd);

void pkg_list_free(struct pkg *, pkg_list);

int pkg_dep_new(struct pkg_dep **);
void pkg_dep_free(struct pkg_dep *);

int pkg_file_new(struct pkg_file **);
void pkg_file_free(struct pkg_file *);

int pkg_dir_new(struct pkg_dir **);
void pkg_dir_free(struct pkg_dir *);

int pkg_option_new(struct pkg_option **);
void pkg_option_free(struct pkg_option *);

int pkg_user_new(struct pkg_user **);
void pkg_user_free(struct pkg_user *);

int pkg_group_new(struct pkg_group **);
void pkg_group_free(struct pkg_group *);

int pkg_jobs_resolv(struct pkg_jobs *jobs);

int pkg_shlib_new(struct pkg_shlib **);
void pkg_shlib_free(struct pkg_shlib *);

int pkg_conflict_new(struct pkg_conflict **);
void pkg_conflict_free(struct pkg_conflict *);

int pkg_provide_new(struct pkg_provide **);
void pkg_provide_free(struct pkg_provide *);

struct packing;

int packing_init(struct packing **pack, const char *path, pkg_formats format);
int packing_append_file_attr(struct packing *pack, const char *filepath,
			     const char *newpath, const char *uname,
			     const char *gname, mode_t perm);
int packing_append_buffer(struct packing *pack, const char *buffer,
			  const char *path, int size);
int packing_append_tree(struct packing *pack, const char *treepath,
			const char *newroot);
int packing_finish(struct packing *pack);
pkg_formats packing_format_from_string(const char *str);

int pkg_delete_files(struct pkg *pkg, unsigned force);
int pkg_delete_dirs(struct pkgdb *db, struct pkg *pkg, bool force);

int pkgdb_is_dir_used(struct pkgdb *db, const char *dir, int64_t *res);

int pkg_conflicts_request_resolve(struct pkg_jobs *j);
int pkg_conflicts_append_pkg(struct pkg *p, struct pkg_jobs *j);
int pkg_conflicts_integrity_check(struct pkg_jobs *j);
void pkg_conflicts_register(struct pkg *p1, struct pkg *p2,
		enum pkg_conflict_type type);

typedef void (*conflict_func_cb)(const char *, const char *, void *);
int pkgdb_integrity_append(struct pkgdb *db, struct pkg *p,
		conflict_func_cb cb, void *cbdata);
int pkgdb_integrity_check(struct pkgdb *db, conflict_func_cb cb, void *cbdata);
struct pkgdb_it *pkgdb_integrity_conflict_local(struct pkgdb *db,
						const char *origin);

int pkg_set_mtree(struct pkg *, const char *mtree);

/* pkgdb commands */
int sql_exec(sqlite3 *, const char *, ...);
int get_pragma(sqlite3 *, const char *sql, int64_t *res, bool silence);
int get_sql_string(sqlite3 *, const char *sql, char **res);

int pkgdb_load_deps(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_rdeps(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_files(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_dirs(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_scripts(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_options(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_mtree(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_category(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_license(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_user(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_group(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_shlib_required(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_shlib_provided(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_annotations(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_conflicts(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_provides(struct pkgdb *db, struct pkg *pkg);

int pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg, int complete, int forced);
int pkgdb_update_shlibs_required(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_update_shlibs_provided(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_update_provides(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_insert_annotations(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_register_finale(struct pkgdb *db, int retcode);

int pkg_register_shlibs(struct pkg *pkg, const char *root);

int pkg_emit_manifest_sbuf(struct pkg*, struct sbuf *, short, char **);
int pkg_emit_filelist(struct pkg *, FILE *);

int do_extract_mtree(char *mtree, const char *prefix);

int pkg_repo_update_binary_pkgs(struct pkg_repo *repo, bool force);

bool ucl_object_emit_sbuf(ucl_object_t *obj, enum ucl_emitter emit_type, struct sbuf **buf);
bool ucl_object_emit_file(ucl_object_t *obj, enum ucl_emitter emit_type, FILE *);

#endif
