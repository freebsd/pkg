/*-
 * Copyright (c) 2011-2015 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include "bsd_compat.h"

#include <sys/param.h>
#include <sys/cdefs.h>
#include <sys/sbuf.h>
#include <sys/types.h>

#include <archive.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <uthash.h>
#include <utlist.h>
#include <ucl.h>

#include "private/utils.h"

#define UCL_COUNT(obj) ((obj)?((obj)->len):0)

#define PKG_NUM_SCRIPTS 9

/*
 * Some compatibility checks
 */
#ifndef MAXLOGNAME
# ifdef LOGIN_NAME_MAX
# define MAXLOGNAME LOGIN_NAME_MAX
# else
# define MAXLOGNAME 64
# endif
#endif
#ifndef __unused
# ifdef __GNUC__
# define __unused __attribute__ ((__unused__))
# else
# define __unused
# endif
#endif

#if ARCHIVE_VERSION_NUMBER < 3000002
#define archive_write_add_filter_xz(a) archive_write_set_compression_xz(a)
#define archive_write_add_filter_bzip2(a) archive_write_set_compression_bzip2(a)
#define archive_write_add_filter_gzip(a) archive_write_set_compression_gzip(a)
#define archive_write_add_filter_none(a) archive_write_set_compression_none(a)
#define archive_read_support_filter_all(a) archive_read_support_compression_all(a)
#define archive_read_support_filter_none(a) archive_read_support_compression_none(a)
#define archive_read_free archive_read_finish
#define archive_write_free archive_write_finish
#define archive_entry_perm archive_entry_mode

#ifndef UF_NOUNLINK
#define UF_NOUNLINK 0
#endif

#ifndef SF_NOUNLINK
#define SF_NOUNLINK 0
#endif

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

#define KHASH_MAP_INIT_STRING(name, khval_t)                                             \
	KHASH_INIT(name, kh_cstr_t, khval_t, 1, string_hash_func, kh_str_hash_equal)

#define kh_string_next(head, data) do {                  \
	khint_t k;                                       \
	if (head == NULL)                                \
		return (EPKG_END);                       \
	if (data == NULL) {                              \
		k = kh_begin(head);                      \
	} else {                                         \
		k = kh_get_strings(head, (data));        \
		k++;                                     \
	}                                                \
	while (k != kh_end(head) && !kh_exist(head, k))  \
		k++;                                     \
	if (k == kh_end(head))                           \
		return (EPKG_END);                       \
	data = kh_value(head, k);                        \
	return (EPKG_OK);                                \
} while (0)

#define kh_next(name, head, data, attrib) do {           \
	khint_t k;                                       \
	if (head == NULL)                                \
		return (EPKG_END);                       \
	if (data == NULL) {                              \
		k = kh_begin(head);                      \
	} else {                                         \
		k = kh_get_##name(head, (data)->attrib); \
		k++;                                     \
	}                                                \
	while (k != kh_end(head) && !kh_exist(head, k))  \
		k++;                                     \
	if (k == kh_end(head)) {                         \
		data = NULL;                             \
		return (EPKG_END);                       \
	}                                                \
	data = kh_value(head, k);                        \
	return (EPKG_OK);                                \
} while (0)

#define kh_free(name, head, type, free_func) do {			\
	if (head) {							\
		type *_todelete;					\
		kh_foreach_value(head, _todelete, free_func(_todelete));\
		kh_destroy_##name(head);				\
		head = NULL;						\
	}								\
} while (0)

#define kh_contains(name, h, v) ((h)?(kh_get_##name(h, v) != kh_end(h)):false)

#define kh_each_value(h, vvar, code)							\
	for (khint_t __i = kh_begin(h); h != NULL && __i != kh_end(h); __i++) {		\
		if (!kh_exist(h, __i)) continue;					\
		(vvar) = kh_val(h, __i);						\
		code;									\
	}

#define kh_count(h) ((h)?((h)->size):0)

#define kh_add(name, h, val, k) do {		\
	int __ret;				\
	khint_t __i;				\
	if (!h) h = kh_init_##name();		\
	__i = kh_put_##name(h, k, &__ret);	\
	if (__ret != 0)				\
		kh_val(h, __i) = val;		\
} while (0)


extern int eventpipe;
extern int64_t debug_level;
extern bool developer_mode;
extern const char *pkg_rootdir;

struct pkg_repo_it;
struct pkg_repo;
struct pkg_message;

KHASH_MAP_INIT_STR(pkg_deps, struct pkg_dep *);
KHASH_MAP_INIT_STR(pkg_files, struct pkg_file *);
KHASH_MAP_INIT_STR(pkg_dirs, struct pkg_dir *);
KHASH_MAP_INIT_STR(pkg_config_files, struct pkg_config_file *);
KHASH_MAP_INIT_STR(strings, char *);

struct pkg {
	bool		 direct;
	bool		 locked;
	bool		 automatic;
	int64_t		 id;
	struct sbuf	*scripts[PKG_NUM_SCRIPTS];
	char			*name;
	char			*origin;
	char			*version;
	char			*old_version;
	char			*maintainer;
	char			*www;
	char			*arch;
	char			*abi;
	char			*uid;
	char			*digest;
	char			*old_digest;
	struct pkg_message	*message;
	char			*prefix;
	char			*comment;
	char			*desc;
	char			*sum;
	char			*repopath;
	char			*reponame;
	char			*repourl;
	char			*reason;
	char			*dep_formula;
	lic_t			 licenselogic;
	int64_t			 pkgsize;
	int64_t			 flatsize;
	int64_t			 old_flatsize;
	int64_t			 timestamp;
	kh_pkg_deps_t		*deps;
	kh_pkg_deps_t		*rdeps;
	kh_strings_t		*categories;
	kh_strings_t		*licenses;
	kh_pkg_files_t		*filehash;
	struct pkg_file		*files;
	kh_pkg_dirs_t		*dirhash;
	struct pkg_dir		*dirs;
	struct pkg_option	*options;
	kh_strings_t		*users;
	kh_strings_t		*groups;
	kh_strings_t		*shlibs_required;
	kh_strings_t		*shlibs_provided;
	struct pkg_conflict *conflicts;
	kh_strings_t		*provides;
	kh_strings_t		*requires;
	kh_pkg_config_files_t	*config_files;
	struct pkg_kv		*annotations;
	unsigned			flags;
	int		rootfd;
	char		rootpath[MAXPATHLEN];
	char		**dir_to_del;
	size_t		dir_to_del_cap;
	size_t		dir_to_del_len;
	pkg_t		 type;
	struct pkg_repo		*repo;
};

struct pkg_dep {
	char		*origin;
	char		*name;
	char		*version;
	char		*uid;
	bool		 locked;
};

typedef enum {
	PKG_MESSAGE_ALWAYS = 0,
	PKG_MESSAGE_INSTALL,
	PKG_MESSAGE_REMOVE,
	PKG_MESSAGE_UPGRADE,
} pkg_message_t;

struct pkg_message {
	char			*str;
	char			*minimum_version;
	char			*maximum_version;
	pkg_message_t		 type;
	struct pkg_message	*prev;
	struct pkg_message	*next;
};

enum pkg_conflict_type {
	PKG_CONFLICT_ALL = 0,
	PKG_CONFLICT_REMOTE_LOCAL,
	PKG_CONFLICT_REMOTE_REMOTE,
	PKG_CONFLICT_LOCAL_LOCAL
};

struct pkg_conflict {
	char *uid;
	char *digest;
	enum pkg_conflict_type type;
	UT_hash_handle	hh;
};

struct pkg_file {
	char		 path[MAXPATHLEN];
	int64_t		 size;
	char		*sum;
	char		 uname[MAXLOGNAME];
	char		 gname[MAXLOGNAME];
	mode_t		 perm;
	u_long		 fflags;
	struct pkg_file	*prev;
	struct pkg_file	*next;
};

struct pkg_dir {
	char		 path[MAXPATHLEN];
	char		 uname[MAXLOGNAME];
	char		 gname[MAXLOGNAME];
	mode_t		 perm;
	u_long		 fflags;
	struct pkg_dir	*prev;
	struct pkg_dir	*next;
};

struct pkg_option {
	char	*key;
	char	*value;
	char	*default_value;
	char	*description;
	UT_hash_handle	hh;
};

struct http_mirror {
	struct url *url;
	struct http_mirror *next;
};

struct pkg_repo_meta_key {
	char *pubkey;
	char *pubkey_type; /* TODO: should be enumeration */
	char *name;
	UT_hash_handle hh;
};

typedef enum pkg_checksum_type_e {
	PKG_HASH_TYPE_SHA256_BASE32 = 0,
	PKG_HASH_TYPE_SHA256_HEX,
	PKG_HASH_TYPE_BLAKE2_BASE32,
	PKG_HASH_TYPE_SHA256_RAW,
	PKG_HASH_TYPE_BLAKE2_RAW,
	PKG_HASH_TYPE_UNKNOWN
} pkg_checksum_type_t;

static const char repo_meta_file[] = "meta";

struct pkg_repo_meta {

	char *maintainer;
	char *source;

	pkg_formats packing_format;
	pkg_checksum_type_t digest_format;

	char *digests;
	char *digests_archive;
	char *manifests;
	char *manifests_archive;
	char *filesite;
	char *filesite_archive;
	char *conflicts;
	char *conflicts_archive;
	char *fulldb;
	char *fulldb_archive;

	char *source_identifier;
	int64_t revision;

	struct pkg_repo_meta_key *keys;

	time_t eol;

	int version;
};

struct pkg_repo_it_ops {
	int (*next)(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags);
	void (*free)(struct pkg_repo_it *it);
	void (*reset)(struct pkg_repo_it *it);
};

struct pkg_repo_it {
	struct pkg_repo *repo;
	struct pkg_repo_it_ops *ops;
	int flags;
	void *data;
};

struct pkg_repo_ops {
	const char *type;
	/* Accessing repo */
	int (*init)(struct pkg_repo *);
	int (*access)(struct pkg_repo *, unsigned);
	int (*open)(struct pkg_repo *, unsigned);
	int (*create)(struct pkg_repo *);
	int (*close)(struct pkg_repo *, bool);

	/* Updating repo */
	int (*update)(struct pkg_repo *, bool);

	/* Query repo */
	struct pkg_repo_it * (*query)(struct pkg_repo *,
					const char *, match_t);
	struct pkg_repo_it * (*shlib_required)(struct pkg_repo *,
					const char *);
	struct pkg_repo_it * (*shlib_provided)(struct pkg_repo *,
					const char *);
	struct pkg_repo_it * (*required)(struct pkg_repo *,
					const char *);
	struct pkg_repo_it * (*provided)(struct pkg_repo *,
					const char *);
	struct pkg_repo_it * (*search)(struct pkg_repo *, const char *, match_t,
					pkgdb_field field, pkgdb_field sort);

	int64_t (*stat)(struct pkg_repo *, pkg_stats_t type);

	int (*ensure_loaded)(struct pkg_repo *repo, struct pkg *pkg, unsigned flags);

	/* Fetch package from repo */
	int (*get_cached_name)(struct pkg_repo *, struct pkg *,
					char *dest, size_t destlen);
	int (*fetch_pkg)(struct pkg_repo *, struct pkg *);
	int (*mirror_pkg)(struct pkg_repo *repo, struct pkg *pkg,
		const char *destdir);
};

typedef enum _pkg_repo_flags {
	REPO_FLAGS_USE_IPV4 = (1U << 0),
	REPO_FLAGS_USE_IPV6 = (1U << 1)
} pkg_repo_flags;

struct pkg_repo {
	struct pkg_repo_ops *ops;

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

	struct fingerprint *trusted_fp;
	struct fingerprint *revoked_fp;

	struct {
		int in;
		int out;
		pid_t pid;
	} sshio;

	struct pkg_repo_meta *meta;

	bool enable;
	UT_hash_handle hh;

	unsigned int priority;

	pkg_repo_flags flags;

	/* Opaque repository data */
	void *priv;
};

struct keyword {
	/* 64 is more than enough for this */
	char keyword[64];
	struct action *actions;
	UT_hash_handle hh;
};

struct plist {
	char last_file[MAXPATHLEN];
	const char *stage;
	char prefix[MAXPATHLEN];
	struct sbuf *pre_install_buf;
	struct sbuf *post_install_buf;
	struct sbuf *pre_deinstall_buf;
	struct sbuf *post_deinstall_buf;
	struct sbuf *pre_upgrade_buf;
	struct sbuf *post_upgrade_buf;
	struct pkg *pkg;
	char *uname;
	char *gname;
	const char *slash;
	char *pkgdep;
	bool ignore_next;
	int64_t flatsize;
	hardlinks_t *hardlinks;
	mode_t perm;
	struct {
		char *buf;
		char **patterns;
		size_t len;
		size_t cap;
	} post_patterns;
	struct keyword *keywords;
};

struct file_attr {
	char *owner;
	char *group;
	mode_t mode;
	u_long fflags;
};

struct action {
	int (*perform)(struct plist *, char *, struct file_attr *);
	struct action *next;
};

struct pkg_config_file {
	char path[MAXPATHLEN];
	char *content;
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

int pkg_fetch_file_to_fd(struct pkg_repo *repo, const char *url, int dest,
    time_t *t, ssize_t offset, int64_t size);
int pkg_repo_fetch_package(struct pkg *pkg);
int pkg_repo_mirror_package(struct pkg *pkg, const char *destdir);
FILE* pkg_repo_fetch_remote_extract_tmp(struct pkg_repo *repo,
		const char *filename, time_t *t, int *rc);
unsigned char *pkg_repo_fetch_remote_extract_mmap(struct pkg_repo *repo,
    const char *filename, time_t *t, int *rc, size_t *sz);
int pkg_repo_fetch_meta(struct pkg_repo *repo, time_t *t);

struct pkg_repo_meta *pkg_repo_meta_default(void);
int pkg_repo_meta_load(const char *file, struct pkg_repo_meta **target);
void pkg_repo_meta_free(struct pkg_repo_meta *meta);
ucl_object_t * pkg_repo_meta_to_ucl(struct pkg_repo_meta *meta);
bool pkg_repo_meta_is_special_file(const char *file, struct pkg_repo_meta *meta);

typedef enum {
	HASH_UNKNOWN,
	HASH_SHA256,
	HASH_BLAKE2
} hash_t;

struct fingerprint {
	hash_t type;
	char hash[BUFSIZ];
	UT_hash_handle hh;
};
int pkg_repo_load_fingerprints(struct pkg_repo *repo);


int pkg_start_stop_rc_scripts(struct pkg *, pkg_rc_attr attr);

int pkg_script_run(struct pkg *, pkg_script type);

int pkg_open2(struct pkg **p, struct archive **a, struct archive_entry **ae,
	      const char *path, struct pkg_manifest_key *keys, int flags, int fd);

int pkg_validate(struct pkg *pkg, struct pkgdb *db);

void pkg_list_free(struct pkg *, pkg_list);

int pkg_kv_new(struct pkg_kv **, const char *key, const char *val);
void pkg_kv_free(struct pkg_kv *);

int pkg_dep_new(struct pkg_dep **);
void pkg_dep_free(struct pkg_dep *);

int pkg_file_new(struct pkg_file **);
void pkg_file_free(struct pkg_file *);

int pkg_dir_new(struct pkg_dir **);
void pkg_dir_free(struct pkg_dir *);

int pkg_option_new(struct pkg_option **);
void pkg_option_free(struct pkg_option *);

int pkg_jobs_resolv(struct pkg_jobs *jobs);

int pkg_conflict_new(struct pkg_conflict **);
void pkg_conflict_free(struct pkg_conflict *);

int pkg_config_file_new(struct pkg_config_file **);
void pkg_config_file_free(struct pkg_config_file *);

struct packing;

int packing_init(struct packing **pack, const char *path, pkg_formats format,
    bool passmode);
int packing_append_file_attr(struct packing *pack, const char *filepath,
     const char *newpath, const char *uname, const char *gname, mode_t perm,
     u_long fflags);
int packing_append_buffer(struct packing *pack, const char *buffer,
			  const char *path, int size);
int packing_append_tree(struct packing *pack, const char *treepath,
			const char *newroot);
void packing_finish(struct packing *pack);
pkg_formats packing_format_from_string(const char *str);
const char* packing_format_to_string(pkg_formats format);

int pkg_delete_files(struct pkg *pkg, unsigned force);
int pkg_delete_dirs(struct pkgdb *db, struct pkg *pkg, struct pkg *p);

/* pkgdb commands */
int sql_exec(sqlite3 *, const char *, ...);
int get_pragma(sqlite3 *, const char *sql, int64_t *res, bool silence);
int get_sql_string(sqlite3 *, const char *sql, char **res);

int pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg, int complete, int forced);
int pkgdb_update_shlibs_required(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_update_shlibs_provided(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_update_provides(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_update_requires(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_insert_annotations(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_register_finale(struct pkgdb *db, int retcode);
int pkgdb_set_pkg_digest(struct pkgdb *db, struct pkg *pkg);
int pkgdb_is_dir_used(struct pkgdb *db, struct pkg *p, const char *dir, int64_t *res);
int pkgdb_file_set_cksum(struct pkgdb *db, struct pkg_file *file, const char *sha256);


int pkg_emit_manifest_sbuf(struct pkg*, struct sbuf *, short, char **);
int pkg_emit_filelist(struct pkg *, FILE *);

bool ucl_object_emit_sbuf(const ucl_object_t *obj, enum ucl_emitter emit_type,
    struct sbuf **buf);
bool ucl_object_emit_file(const ucl_object_t *obj, enum ucl_emitter emit_type,
    FILE *);

pkg_object* pkg_emit_object(struct pkg *pkg, short flags);

/* Hash is in format <version>:<typeid>:<hexhash> */
#define PKG_CHECKSUM_SHA256_LEN (SHA256_DIGEST_LENGTH * 2 + 1)
#define PKG_CHECKSUM_BLAKE2_LEN (BLAKE2B_OUTBYTES * 8 / 5 + sizeof("100") * 2 + 2)
#define PKG_CHECKSUM_CUR_VERSION 2

int pkg_checksum_generate(struct pkg *pkg, char *dest, size_t destlen,
	pkg_checksum_type_t type);

/*
 * Calculates checksum for any data.
 * Caller must free resulting hash after usage
 */
unsigned char * pkg_checksum_data(const unsigned char *in, size_t inlen,
	pkg_checksum_type_t type);
unsigned char *pkg_checksum_fd(int fd, pkg_checksum_type_t type);
unsigned char *pkg_checksum_file(const char *path, pkg_checksum_type_t type);
unsigned char *pkg_checksum_fileat(int fd, const char *path,
    pkg_checksum_type_t type);
unsigned char *pkg_checksum_symlink(const char *path, const char *root,
    pkg_checksum_type_t type);
unsigned char *pkg_checksum_symlinkat(int fd, const char *path,
    const char *root, pkg_checksum_type_t type);
int pkg_checksum_validate_file(const char *path, const  char *sum);
int pkg_checksum_validate_fileat(int fd, const char *path, const  char *sum);

bool pkg_checksum_is_valid(const char *cksum, size_t clen);
pkg_checksum_type_t pkg_checksum_get_type(const char *cksum, size_t clen);
pkg_checksum_type_t pkg_checksum_file_get_type(const char *cksum, size_t clen);
pkg_checksum_type_t pkg_checksum_type_from_string(const char *name);
const char* pkg_checksum_type_to_string(pkg_checksum_type_t type);
size_t pkg_checksum_type_size(pkg_checksum_type_t type);
int pkg_checksum_calculate(struct pkg *pkg, struct pkgdb *db);
char *pkg_checksum_generate_file(const char *path, pkg_checksum_type_t type);
char *pkg_checksum_generate_fileat(int fd, const char *path,
    pkg_checksum_type_t type);

int pkg_add_upgrade(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location,
    struct pkg *rp, struct pkg *lp);
void pkg_delete_dir(struct pkg *pkg, struct pkg_dir *dir);
void pkg_delete_file(struct pkg *pkg, struct pkg_file *file, unsigned force);
int pkg_open_root_fd(struct pkg *pkg);
void pkg_add_dir_to_del(struct pkg *pkg, const char *file, const char *dir);
struct plist *plist_new(struct pkg *p, const char *stage);
int plist_parse_line(struct plist *p, char *line);
void plist_free(struct plist *);
int pkg_appendscript(struct pkg *pkg, const char *cmd, pkg_script type);

int pkg_addscript(struct pkg *pkg, const char *data, pkg_script type);
int pkg_addfile(struct pkg *pkg, const char *path, const char *sha256,
    bool check_duplicates);
int pkg_addfile_attr(struct pkg *pkg, const char *path, const char *sha256,
    const char *uname, const char *gname, mode_t perm, u_long fflags,
    bool check_duplicates);

int pkg_adddir(struct pkg *pkg, const char *path, bool check_duplicates);
int pkg_adddir_attr(struct pkg *pkg, const char *path, const char *uname,
    const char *gname, mode_t perm, u_long fflags, bool check_duplicates);

int pkg_addstring(kh_strings_t **s, const char *value, const char *title);
int pkg_kv_add(struct pkg_kv **kv, const char *key, const char *value, const char *title);
const char *pkg_kv_get(struct pkg_kv *const*kv, const char *key);
int pkg_adduser(struct pkg *pkg, const char *name);
int pkg_addgroup(struct pkg *pkg, const char *group);
int pkg_addshlib_required(struct pkg *pkg, const char *name);
int pkg_addshlib_provided(struct pkg *pkg, const char *name);
int pkg_addconflict(struct pkg *pkg, const char *name);
int pkg_addprovide(struct pkg *pkg, const char *name);
int pkg_addrequire(struct pkg *pkg, const char *name);
int pkg_addconfig_file(struct pkg *pkg, const char *name, const char *buf);

int pkg_addoption(struct pkg *pkg, const char *name, const char *value);
int pkg_addoption_default(struct pkg *pkg, const char *key, const char *default_value);
int pkg_addoption_description(struct pkg *pkg, const char *key, const char *description);

int pkg_arch_to_legacy(const char *arch, char *dest, size_t sz);
bool pkg_is_config_file(struct pkg *p, const char *path, const struct pkg_file **file, struct pkg_config_file **cfile);
int pkg_message_from_ucl(struct pkg *pkg, const ucl_object_t *obj);
int pkg_message_from_str(struct pkg *pkg, const char *str, size_t len);
ucl_object_t* pkg_message_to_ucl(const struct pkg *pkg);
char* pkg_message_to_str(struct pkg *pkg);

#endif
