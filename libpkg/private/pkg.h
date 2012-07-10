/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/types.h>

#include <archive.h>
#include <sqlite3.h>
#include <openssl/sha.h>
#include <stdbool.h>

#include "private/utils.h"

#define PKG_NUM_FIELDS 18
#define PKG_NUM_SCRIPTS 8

#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM | \
		ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_ACL | \
		ARCHIVE_EXTRACT_FFLAGS|ARCHIVE_EXTRACT_XATTR)

#define LIST_FREE(head, data, free_func) do { \
	while (!STAILQ_EMPTY(head)) { \
		data = STAILQ_FIRST(head); \
		STAILQ_REMOVE_HEAD(head, next); \
		free_func(data); \
	}  \
	} while (0)

struct pkg {
	struct sbuf * fields[PKG_NUM_FIELDS];
	bool automatic;
	int64_t flatsize;
	int64_t new_flatsize;
	int64_t new_pkgsize;
	struct sbuf * scripts[PKG_NUM_SCRIPTS];
	STAILQ_HEAD(categories, pkg_category) categories;
	STAILQ_HEAD(licenses, pkg_license) licenses;
	STAILQ_HEAD(deps, pkg_dep) deps;
	STAILQ_HEAD(rdeps, pkg_dep) rdeps;
	STAILQ_HEAD(files, pkg_file) files;
	STAILQ_HEAD(dirs, pkg_dir) dirs;
	STAILQ_HEAD(options, pkg_option) options;
	STAILQ_HEAD(users, pkg_user) users;
	STAILQ_HEAD(groups, pkg_group) groups;
	STAILQ_HEAD(shlibs, pkg_shlib) shlibs;
	int flags;
	int64_t rowid;
	int64_t time;
	lic_t licenselogic;
	pkg_t type;
	STAILQ_ENTRY(pkg) next;
};

struct pkg_dep {
	struct sbuf *origin;
	struct sbuf *name;
	struct sbuf *version;
	STAILQ_ENTRY(pkg_dep) next;
};

struct pkg_license {
	struct sbuf *name;
	STAILQ_ENTRY(pkg_license) next;
};

struct pkg_category {
	struct sbuf *name;
	STAILQ_ENTRY(pkg_category) next;
};

struct pkg_file {
	char path[MAXPATHLEN +1];
	char sum[SHA256_DIGEST_LENGTH * 2 +1];
	char uname[MAXLOGNAME +1];
	char gname[MAXLOGNAME +1];
	int keep;
	mode_t perm;
	STAILQ_ENTRY(pkg_file) next;
};

struct pkg_dir {
	char path[MAXPATHLEN +1];
	char uname[MAXLOGNAME +1];
	char gname[MAXLOGNAME +1];
	mode_t perm;
	int keep;
	bool try;
	STAILQ_ENTRY(pkg_dir) next;
};

struct pkg_option {
	struct sbuf *key;
	struct sbuf *value;
	STAILQ_ENTRY(pkg_option) next;
};

struct pkg_jobs {
	STAILQ_HEAD(jobs, pkg) jobs;
	struct pkgdb *db;
	pkg_jobs_t type;
};

struct pkg_jobs_node {
	struct pkg *pkg;
	size_t nrefs;
	struct pkg_jobs_node **parents; /* rdeps */
	size_t parents_len;
	size_t parents_cap;
	LIST_ENTRY(pkg_jobs_node) entries;
};

struct pkg_user {
	char name[MAXLOGNAME+1];
	char uidstr[8192];/* taken from pw_util.c */
	STAILQ_ENTRY(pkg_user) next;
};

struct pkg_group {
	char name[MAXLOGNAME+1];
	char gidstr[8192]; /* taken from gw_util.c */
	STAILQ_ENTRY(pkg_group) next;
};

struct pkg_shlib {
	struct sbuf *name;
	STAILQ_ENTRY(pkg_shlib) next;
};


/* sql helpers */

typedef struct _sql_prstmt {
	sqlite3_stmt *stmt;
	const char *sql;
	const char *argtypes;
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
int pkg_delete(struct pkg *pkg, struct pkgdb *db, int flags);
#define PKG_DELETE_FORCE (1<<0)
#define PKG_DELETE_UPGRADE (1<<1)

int pkg_repo_fetch(struct pkg *pkg);

int pkg_start_stop_rc_scripts(struct pkg *, pkg_rc_attr attr);

int pkg_script_run(struct pkg *, pkg_script type);

int pkg_add_user_group(struct pkg *pkg);
int pkg_delete_user_group(struct pkgdb *db, struct pkg *pkg);

int pkg_open2(struct pkg **p, struct archive **a, struct archive_entry **ae, const char *path, struct sbuf *mbuf);

void pkg_list_free(struct pkg *, pkg_list);

int pkg_dep_new(struct pkg_dep **);
void pkg_dep_free(struct pkg_dep *);

int pkg_file_new(struct pkg_file **);
void pkg_file_free(struct pkg_file *);

int pkg_dir_new(struct pkg_dir **);
void pkg_dir_free(struct pkg_dir *);

int pkg_category_new(struct pkg_category **);
void pkg_category_free(struct pkg_category *);

int pkg_license_new(struct pkg_license **);
void pkg_license_free(struct pkg_license *);

int pkg_option_new(struct pkg_option **);
void pkg_option_free(struct pkg_option *);

int pkg_user_new(struct pkg_user **);
void pkg_user_free(struct pkg_user *);

int pkg_group_new(struct pkg_group **);
void pkg_group_free(struct pkg_group *);

int pkg_jobs_resolv(struct pkg_jobs *jobs);

int pkg_shlib_new(struct pkg_shlib **);
void pkg_shlib_free(struct pkg_shlib *);

struct packing;

int packing_init(struct packing **pack, const char *path, pkg_formats format);
int packing_append_file(struct packing *pack, const char *filepath, const char *newpath);
int packing_append_file_attr(struct packing *pack, const char *filepath, const char *newpath, const char *uname, const char *gname, mode_t perm);
int packing_append_buffer(struct packing *pack, const char *buffer, const char *path, int size);
int packing_append_tree(struct packing *pack, const char *treepath, const char *newroot);
int packing_finish(struct packing *pack);
pkg_formats packing_format_from_string(const char *str);

int pkg_delete_files(struct pkg *pkg, int force);
int pkg_delete_dirs(struct pkgdb *db, struct pkg *pkg, int force);

int pkgdb_is_dir_used(struct pkgdb *db, const char *dir, int64_t *res);

int pkgdb_integrity_append(struct pkgdb *db, struct pkg *p);
int pkgdb_integrity_check(struct pkgdb *db);
struct pkgdb_it *pkgdb_integrity_conflict_local(struct pkgdb *db, const char *origin);

int pkg_set_mtree(struct pkg *, const char *mtree);

/* pkg repo related */
int pkg_check_repo_version(struct pkgdb *db, const char *database);

/* pkgdb commands */
int sql_exec(sqlite3 *, const char *, ...);

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
int pkgdb_load_shlib(struct pkgdb *db, struct pkg *pkg);

int pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg, int complete);
int pkgdb_update_shlibs(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_register_finale(struct pkgdb *db, int retcode);

#endif
