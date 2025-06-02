/*-
 * Copyright (c) 2011-2024 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2013-2017 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

#include "bsd_compat.h"

#include <sys/param.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#include <archive.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <utlist.h>
#include <ucl.h>

#include "xmalloc.h"
#include "private/pkg_abi.h"
#include "private/utils.h"
#include "private/fetch.h"
#include "pkghash.h"

#define PKG_NUM_SCRIPTS 9
#define PKG_NUM_LUA_SCRIPTS 5

#define PKG_HASH_SEP '~'
#define PKG_HASH_SEPSTR "~"

#define PKG_HASH_DIR "Hashed"

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

#ifndef roundup2
#define	roundup2(x, y)	(((x)+((y)-1))&(~((y)-1))) /* if y is powers of two */
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


#define LL_FREE2(head, free_func, next) do {   \
	__typeof(head) l1, l2;                 \
	LL_FOREACH_SAFE2(head, l1, l2, next) {       \
		LL_DELETE2(head, l1, next);          \
		free_func(l1);                \
	}                                     \
	head = NULL;                          \
} while (0)
#define LL_FREE(head, free_func) LL_FREE2(head, free_func, next)

#define DL_FREE2(head, free_func, prev, next) do {   \
	__typeof(head) l1, l2;                 \
	DL_FOREACH_SAFE2(head, l1, l2, next) {       \
		DL_DELETE2(head, l1, prev, next);          \
		free_func(l1);                \
	}                                     \
	head = NULL;                          \
} while (0)
#define DL_FREE(head, free_func) DL_FREE2(head, free_func, prev, next)

typedef vec_t(struct pkg_kv *) kvlist_t;

typedef enum {
	IPALL = 0,
	IPV4,
	IPV6,
} ip_version_t;

struct pkg_kvlist {
	kvlist_t *list;
};

struct pkg_stringlist {
	charv_t *list;
};

struct pkg_kvlist_iterator {
	kvlist_t *list;
	size_t pos;
};

struct pkg_stringlist_iterator {
	charv_t *list;
	size_t pos;
};

struct pkg_ctx {
	int eventpipe;
	int64_t debug_level;
	uint64_t debug_flags;
	bool developer_mode;
	const char *pkg_rootdir;
	const char *dbdir;
	const char *cachedir;
	const char *compression_format;
	int compression_level;
	int compression_threads;
	int rootfd;
	int cachedirfd;
	int devnullfd;
	int dbdirfd;
	int pkg_dbdirfd;
	int pkg_reposdirfd;
	bool archive_symlink;
	bool backup_libraries;
	const char *backup_library_path;
	bool triggers;
	const char *triggers_path;
	pkghash *touched_dir_hash;
	bool defer_triggers;
	bool repo_accept_legacy_pkg;
	ip_version_t ip;
	struct pkg_abi abi;
	bool ischrooted;
	bool no_version_for_deps;
	bool track_linux_compat_shlibs;
};

extern struct pkg_ctx ctx;

struct pkg_repo_content {
	time_t mtime;
	int manifest_fd;
	size_t manifest_len;
	int data_fd;
};

struct pkgsign_ctx;

struct pkg_repo_it;
struct pkg_repo;
struct url;
struct fetcher;
struct pkg_message;
typedef vec_t(struct pkg_message *) messages_t;

struct pkg {
	bool		 direct;
	bool		 locked;
	bool		 automatic;
	bool		 vital;
	bool		 list_sorted;
	int64_t		 id;
	xstring		*scripts[PKG_NUM_SCRIPTS];
	charv_t	 lua_scripts[PKG_NUM_LUA_SCRIPTS];
	char			*name;
	char			*origin;
	char			*version;
	char			*old_version;
	char			*maintainer;
	char			*www;
	char			*altabi;
	char			*abi;
	char			*uid;
	char			*digest;
	char			*old_digest;
	messages_t		 message;
	char			*prefix;
	char			*oprefix;
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
	pkghash			*depshash;
	struct pkg_dep		*depends;
	pkghash			*rdepshash;
	struct pkg_dep		*rdepends;
	charv_t		 categories;
	charv_t		 licenses;
	pkghash			*filehash;
	struct pkg_file		*files;
	pkghash			*dirhash;
	struct pkg_dir		*dirs;
	pkghash			*optionshash;
	struct pkg_option	*options;
	charv_t		 users;
	charv_t		 groups;
	charv_t		 shlibs_required;
	charv_t		 shlibs_provided;
	pkghash			*conflictshash;
	struct pkg_conflict	*conflicts;
	charv_t		 provides;
	charv_t		 requires;
	pkghash			*config_files_hash;
	struct pkg_config_file	*config_files;
	kvlist_t		 annotations;
	unsigned			flags;
	int		rootfd;
	char		rootpath[MAXPATHLEN];
	charv_t	dir_to_del;
	pkg_t		 type;
	struct pkg_repo		*repo;
};
typedef vec_t(struct pkg *) pkgs_t;

DEFINE_VEC_INSERT_SORTED_PROTO(pkgs_t, pkgs, struct pkg *);
struct pkg **pkgs_search(pkgs_t *, char *);
void pkgs_sort(pkgs_t *);
bool append_pkg_if_newer(pkgs_t *pkgs, struct pkg *p);

typedef enum {
	SCRIPT_UNKNOWN = 0,
	SCRIPT_SHELL,
	SCRIPT_LUA,
} script_type_t;

struct trigger {
	char *name;
	ucl_object_t *path;
	ucl_object_t *path_glob;
	ucl_object_t *path_regex;
	struct {
		char *script;
		int type;
		bool sandbox;
	} script;
	struct {
		char *script;
		int type;
		bool sandbox;
	} cleanup;
	pkghash *matched;
};
typedef vec_t(struct trigger *) trigger_t;

struct triggers {
	ucl_object_t *schema;
	int dfd;
	trigger_t *cleanup;
	trigger_t *post_transaction;
	trigger_t *post_install;
};

struct pkg_create {
	bool overwrite;
	bool expand_manifest;
	int compression_level;
	int compression_threads;
	pkg_formats format;
	time_t timestamp;
	const char *rootdir;
	const char *outdir;
};

struct pkg_repo_create {
	bool filelist;
	bool hash;
	bool hash_symlink;
	const char *outdir;
	int ofd;
	const char *metafile;
	struct pkg_repo_meta *meta;
	ucl_object_t *groups;
	ucl_object_t *expired_packages;
	struct {
		char **argv;
		int argc;
		pkg_password_cb *cb;
	} sign;
};

struct pkg_dep {
	char		*origin;
	char		*name;
	char		*version;
	char		*uid;
	bool		 locked;
	struct pkg_dep		*alt_next, *alt_prev; /* Chain of alternatives */
	struct pkg_dep	*next, *prev;
};

typedef enum {
	PKG_FILE_NONE = 0,
	PKG_FILE_EXIST,
	PKG_FILE_SAVE,
} file_previous_t;

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
	struct pkg_conflict *next, *prev;
};

typedef enum {
	MERGE_NOTNEEDED = 0,
	MERGE_FAILED,
	MERGE_SUCCESS,
	MERGE_NOT_LOCAL,
} merge_status;

struct pkg_config_file {
	char path[MAXPATHLEN];
	char *content;
	char *newcontent;
	merge_status status;
	struct pkg_config_file *next, *prev;
};

struct pkg_file {
	char		 path[MAXPATHLEN];
	int64_t		 size;
	char		*sum;
	char		 uname[MAXLOGNAME];
	char		 gname[MAXLOGNAME];
	mode_t		 perm;
	uid_t		 uid;
	gid_t		 gid;
	char		 temppath[MAXPATHLEN];
	u_long		 fflags;
	struct pkg_config_file *config;
	struct timespec	 time[2];
	struct pkg_file	*next, *prev;
	file_previous_t	 previous;
};

struct pkg_dir {
	char		 path[MAXPATHLEN];
	char		 uname[MAXLOGNAME];
	char		 gname[MAXLOGNAME];
	mode_t		 perm;
	u_long		 fflags;
	uid_t		 uid;
	gid_t		 gid;
	bool		 noattrs;
	struct timespec	 time[2];
	struct pkg_dir	*next, *prev;
};

struct pkg_option {
	char	*key;
	char	*value;
	char	*default_value;
	char	*description;
	struct pkg_option *next, *prev;
};

struct http_mirror;

struct pkg_repo_meta_key {
	char *pubkey;
	char *pubkey_type; /* TODO: should be enumeration */
	char *name;
};

typedef enum pkg_checksum_type_e {
	PKG_HASH_TYPE_SHA256_BASE32 = 0,
	PKG_HASH_TYPE_SHA256_HEX,
	PKG_HASH_TYPE_BLAKE2_BASE32,
	PKG_HASH_TYPE_SHA256_RAW,
	PKG_HASH_TYPE_BLAKE2_RAW,
	PKG_HASH_TYPE_BLAKE2S_BASE32,
	PKG_HASH_TYPE_BLAKE2S_RAW,
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
	char *data;
	char *data_archive;
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

	pkghash *keys;

	time_t eol;

	int version;
	char *repopath;
	bool hash;
	bool hash_symlink;
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
					const char*, const char *, match_t);
	struct pkg_repo_it * (*groupquery)(struct pkg_repo *,
					const char*, match_t);
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
	struct pkg_repo_it * (*groupsearch)(struct pkg_repo *, const char *,
	    match_t, pkgdb_field field);

	int64_t (*stat)(struct pkg_repo *, pkg_stats_t type);

	int (*ensure_loaded)(struct pkg_repo *repo, struct pkg *pkg, unsigned flags);

	/* Fetch package from repo */
	int (*get_cached_name)(struct pkg_repo *, struct pkg *,
					char *dest, size_t destlen);
	int (*fetch_pkg)(struct pkg_repo *, struct pkg *);
	int (*mirror_pkg)(struct pkg_repo *repo, struct pkg *pkg,
		const char *destdir);
};

struct pkg_key {
	struct pkgsign_ctx	*ctx;
};

struct pkg_repo {
	struct pkg_repo_ops *ops;

	char *name;
	struct fetcher *fetcher;
	char *url;
	char *pubkey;
	mirror_t mirror_type;
	union {
		struct dns_srvinfo *srv;
		struct http_mirror *http;
	};
	signature_t signature_type;
	char *fingerprints;
	FILE *fh;
	void *fetch_priv;
	bool silent;

	pkghash *trusted_fp;
	pkghash *revoked_fp;

	struct {
		int in;
		int out;
		pid_t pid;
	} sshio;

	struct pkg_repo_meta *meta;

	bool enable;

	unsigned int priority;

	ip_version_t ip;
	kvlist_t env;
	int dfd;
	char *dbpath;

	/* Opaque repository data */
	void *priv;
	struct pkg_repo *next, *prev;
};

typedef vec_t(struct action *) actions_t;
struct keyword {
	char *keyword;
	actions_t actions;
};

struct plist {
	char last_file[MAXPATHLEN];
	const char *stage;
	int stagefd;
	bool in_include;
	int plistdirfd;
	char prefix[MAXPATHLEN];
	xstring *pre_install_buf;
	xstring *post_install_buf;
	xstring *pre_deinstall_buf;
	xstring *post_deinstall_buf;
	struct pkg *pkg;
	char *uname;
	char *gname;
	const char *slash;
	int64_t flatsize;
	hardlinks_t hardlinks;
	mode_t perm;
	pkghash *keywords;
	kvlist_t variables;
};

struct file_attr {
	char *owner;
	char *group;
	mode_t mode;
	u_long fflags;
};

struct action {
	int (*perform)(struct plist *, char *, struct file_attr *);
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
 * @param rpkg A package which will replace pkg, or NULL
 * @param db An opened pkgdb
 * @return An error code.
 */
int pkg_delete(struct pkg *pkg, struct pkg *rpkg, struct pkgdb *db, int flags,
    struct triggers *);
#define PKG_DELETE_UPGRADE	(1 << 1)	/* delete as a split upgrade */
#define PKG_DELETE_NOSCRIPT	(1 << 2)	/* don't run delete scripts */
#define PKG_DELETE_NOEXEC	(1 << 3)	/* don't run delete scripts which execute things*/

int pkg_fetch_file_to_fd(struct pkg_repo *repo, int dest, struct fetch_item *,
    bool silent);
int pkg_repo_open(struct pkg_repo *repo);
int pkg_repo_fetch_package(struct pkg *pkg);
int pkg_repo_mirror_package(struct pkg *pkg, const char *destdir);
int pkg_repo_fetch_remote_extract_fd(struct pkg_repo *repo, struct pkg_repo_content *);
int pkg_repo_meta_dump_fd(struct pkg_repo_meta *target, const int fd);
int pkg_repo_fetch_meta(struct pkg_repo *repo, time_t *t);
int pkg_repo_fetch_remote_extract_fd(struct pkg_repo *repo, struct pkg_repo_content *);
int pkg_repo_fetch_data_fd(struct pkg_repo *repo, struct pkg_repo_content *);

struct pkg_repo_meta *pkg_repo_meta_default(void);
int pkg_repo_meta_load(const int fd, struct pkg_repo_meta **target);
void pkg_repo_meta_free(struct pkg_repo_meta *meta);
ucl_object_t * pkg_repo_meta_to_ucl(struct pkg_repo_meta *meta);
bool pkg_repo_meta_is_special_file(const char *file, struct pkg_repo_meta *meta);
bool pkg_repo_meta_is_old_file(const char *file, struct pkg_repo_meta *meta);

typedef enum {
	HASH_UNKNOWN,
	HASH_SHA256,
	HASH_BLAKE2
} hash_t;

struct fingerprint {
	hash_t type;
	char hash[BUFSIZ];
};
int pkg_repo_load_fingerprints(struct pkg_repo *repo);


int pkg_start_stop_rc_scripts(struct pkg *, pkg_rc_attr attr);

int pkg_script_run(struct pkg *, pkg_script type, bool upgrade, bool noexec);
int pkg_lua_script_run(struct pkg *, pkg_lua_script type, bool upgrade);
ucl_object_t *pkg_lua_script_to_ucl(charv_t *);
int pkg_script_run_child(int pid, int *pstat, int inputfd, const char* script_name);

int pkg_open2(struct pkg **p, struct archive **a, struct archive_entry **ae,
	      const char *path, int flags, int fd);

int pkg_validate(struct pkg *pkg, struct pkgdb *db);

void pkg_list_free(struct pkg *, pkg_list);

struct pkg_kv *pkg_kv_new(const char *key, const char *val);
void pkg_kv_free(struct pkg_kv *);
struct pkg_kv *pkg_kv_search(kvlist_t *, char *);
void pkg_kv_sort(kvlist_t *);
DEFINE_VEC_INSERT_SORTED_PROTO(kvlist_t, pkg_kv, struct pkg_kv *);

void pkg_dep_free(struct pkg_dep *);
void pkg_file_free(struct pkg_file *);
void pkg_option_free(struct pkg_option *);
void pkg_conflict_free(struct pkg_conflict *);
void pkg_config_file_free(struct pkg_config_file *);

struct iovec;
struct packing;

int packing_init(struct packing **pack, const char *path, pkg_formats format, int clevel, int threads, time_t timestamp, bool overwrite, bool archive_symlink);
int packing_append_file_attr(struct packing *pack, const char *filepath,
     const char *newpath, const char *uname, const char *gname, mode_t perm,
     u_long fflags);
int packing_append_iovec(struct packing *pack, const char *buffer,
			  struct iovec *iov, int niov);
int packing_append_buffer(struct packing *pack, const char *buffer,
			  const char *path, int size);
void packing_get_filename(struct packing *pack, const char *filename);
void packing_finish(struct packing *pack);
pkg_formats packing_format_from_string(const char *str);
const char* packing_format_to_string(pkg_formats format);
bool packing_is_valid_format(const char *str);

int pkg_delete_files(struct pkg *pkg, struct pkg *rpkg, int flags,
    struct triggers *t);
int pkg_delete_dirs(struct pkgdb *db, struct pkg *pkg, struct pkg *p);

/* pkgdb commands */
int sql_exec(sqlite3 *, const char *, ...);
int get_pragma(sqlite3 *, const char *sql, int64_t *res, bool silence);

int pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg, int forced, const char *);
int pkgdb_update_shlibs_required(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_update_shlibs_provided(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_update_provides(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_update_requires(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_insert_annotations(struct pkg *pkg, int64_t package_id, sqlite3 *s);
int pkgdb_register_finale(struct pkgdb *db, int retcode, const char *);
int pkgdb_set_pkg_digest(struct pkgdb *db, struct pkg *pkg);
int pkgdb_is_dir_used(struct pkgdb *db, struct pkg *p, const char *dir, int64_t *res);
int pkgdb_file_set_cksum(struct pkgdb *db, struct pkg_file *file, const char *sha256);

int pkg_emit_filelist(struct pkg *, FILE *);

bool ucl_object_emit_buf(const ucl_object_t *obj, enum ucl_emitter emit_type,
    xstring **buf);
bool ucl_object_emit_fd(const ucl_object_t *obj, enum ucl_emitter emit_type,
    int fd);
bool ucl_object_emit_file(const ucl_object_t *obj, enum ucl_emitter emit_type,
    FILE *);

pkg_object* pkg_emit_object(struct pkg *pkg, short flags);

int pkg_checksum_generate(struct pkg *pkg, char *dest, size_t destlen,
    pkg_checksum_type_t type, bool inc_scripts, bool inc_version, bool inc_files);

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
unsigned char *pkg_checksum_symlink(const char *path,
    pkg_checksum_type_t type);
unsigned char *pkg_checksum_symlinkat(int fd, const char *path,
    pkg_checksum_type_t type);
int pkg_checksum_validate_file(const char *path, const  char *sum);
int pkg_checksum_validate_fileat(int fd, const char *path, const  char *sum);

bool pkg_checksum_is_valid(const char *cksum, size_t clen);
pkg_checksum_type_t pkg_checksum_get_type(const char *cksum, size_t clen);
pkg_checksum_type_t pkg_checksum_file_get_type(const char *cksum, size_t clen);
pkg_checksum_type_t pkg_checksum_type_from_string(const char *name);
const char* pkg_checksum_type_to_string(pkg_checksum_type_t type);
size_t pkg_checksum_type_size(pkg_checksum_type_t type);
int pkg_checksum_calculate(struct pkg *pkg, struct pkgdb *db, bool inc_scripts,
    bool inc_version, bool inc_files);
char *pkg_checksum_generate_file(const char *path, pkg_checksum_type_t type);
char *pkg_checksum_generate_fileat(int fd, const char *path,
    pkg_checksum_type_t type);

int pkg_add_group(struct pkg *pkg);
int pkg_add_upgrade(struct pkgdb *db, const char *path, unsigned flags,
    const char *location, struct pkg *rp, struct pkg *lp, struct triggers *);
int pkg_add_from_remote(struct pkgdb *db, const char *path, unsigned flags,
    const char *location, struct pkg *rp, struct triggers *);
void pkg_delete_dir(struct pkg *pkg, struct pkg_dir *dir);
void pkg_delete_file(struct pkg *pkg, struct pkg_file *file);
int pkg_open_root_fd(struct pkg *pkg);
void pkg_add_dir_to_del(struct pkg *pkg, const char *file, const char *dir);
struct plist *plist_new(struct pkg *p, const char *stage);
int plist_parse_line(struct plist *p, char *line);
char *extract_keywords(char *line, char **keyword, struct file_attr **attr);
struct file_attr *parse_keyword_args(char *args, char *keyword);
void plist_free(struct plist *);
int pkg_appendscript(struct pkg *pkg, const char *cmd, pkg_script type);
void free_file_attr(struct file_attr *a);

int pkg_add_lua_script(struct pkg *pkg, const char *data, pkg_lua_script type);
int pkg_addscript(struct pkg *pkg, const char *data, pkg_script type);
int pkg_addfile(struct pkg *pkg, const char *path, const char *sha256,
    bool check_duplicates);
int pkg_addfile_attr(struct pkg *pkg, const char *path, const char *sha256,
    const char *uname, const char *gname, mode_t perm, u_long fflags,
    bool check_duplicates);

int pkg_adddir(struct pkg *pkg, const char *path, bool check_duplicates);
int pkg_adddir_attr(struct pkg *pkg, const char *path, const char *uname,
    const char *gname, mode_t perm, u_long fflags, bool check_duplicates);

int pkg_addstring(charv_t *s, const char *value, const char *title);
int pkg_kv_add(kvlist_t *kv, const char *key, const char *value, const char *title);
const char *pkg_kv_get(const kvlist_t *kv, const char *key);
int pkg_adduser(struct pkg *pkg, const char *name);
int pkg_addgroup(struct pkg *pkg, const char *group);

enum pkg_shlib_flags {
	PKG_SHLIB_FLAGS_NONE = 0,
	PKG_SHLIB_FLAGS_COMPAT_32 = 1 << 0,
	PKG_SHLIB_FLAGS_COMPAT_LINUX = 1 << 1,
};
/* Determine shlib flags by comparing the shlib abi with ctx.abi */
enum pkg_shlib_flags pkg_shlib_flags_from_abi(const struct pkg_abi *shlib_abi);
/*
 * Given an unadorned shlib name (e.g. libfoo.so.1.0.0) return a newly allocated
 * string with the given flags appended (e.g. libfoo.so.1.0.0:Linux:32).
 */
char *pkg_shlib_name_with_flags(const char *name, enum pkg_shlib_flags flags);
int pkg_addshlib_required(struct pkg *pkg, const char *name, enum pkg_shlib_flags);
/* No checking for duplicates or filtering */
int pkg_addshlib_required_raw(struct pkg *pkg, const char *name);
int pkg_addshlib_provided(struct pkg *pkg, const char *name, enum pkg_shlib_flags);

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
int pkg_lua_script_from_ucl(struct pkg *pkg, const ucl_object_t *obj, pkg_lua_script);
char* pkg_message_to_str(struct pkg *pkg);

int metalog_open(const char *metalog);
int metalog_add(int type, const char *path, const char *uname,
    const char *gname, int mode, unsigned long fflags, const char *link);
void metalog_close();
enum pkg_metalog_type {
	PKG_METALOG_FILE = 0,
	PKG_METALOG_DIR,
	PKG_METALOG_LINK,
};

int pkg_set_from_fileat(int fd, struct pkg *pkg, pkg_attr attr, const char *file, bool trimcr);
void pkg_rollback_cb(void *);
void pkg_rollback_pkg(struct pkg *);
int pkg_add_fromdir(struct pkg *, const char *, struct pkgdb *db);
struct pkg_dep* pkg_adddep_chain(struct pkg_dep *chain,
		struct pkg *pkg, const char *name, const char *origin, const
		char *version, bool locked);
void backup_library(struct pkgdb *, struct pkg *, const char *);
int suggest_arch(struct pkg *, bool);
int set_attrsat(int fd, const char *path, mode_t perm, uid_t uid, gid_t gid, const struct timespec *ats, const struct timespec *mts);

trigger_t *triggers_load(bool cleanup_only);
int triggers_execute(trigger_t *cleanup_triggers);
void trigger_is_it_a_cleanup(struct triggers *t, const char *path);
void trigger_free(struct trigger *);
void append_touched_dir(const char *path);
void append_touched_file(const char *path);

int pkg_parse_manifest_ucl(struct pkg *pkg, ucl_object_t *o);
int pkg_get_reposdirfd(void);
char * expand_plist_variables(const char *in, kvlist_t *vars);

int scan_system_shlibs(charv_t *system_shlibs, const char *rootdir);
void pkg_lists_sort(struct pkg *p);
void pkg_cleanup_shlibs_required(struct pkg *pkg, charv_t *internal_provided);

#endif
