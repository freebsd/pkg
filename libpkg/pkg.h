#ifndef _PKG_H
#define _PKG_H

struct pkg;
struct pkg_file;
struct pkg_conflict;
struct pkg_exec;
struct pkg_script;

struct pkgdb;
struct pkgdb_it;

typedef enum _match_t {
	MATCH_ALL,
	MATCH_EXACT,
	MATCH_GLOB,
	MATCH_REGEX,
	MATCH_EREGEX
} match_t;

typedef enum {
	PKG_FILE,
	PKG_REMOTE,
	PKG_INSTALLED,
	PKG_NOTFOUND
} pkg_t;

typedef enum {
	PKG_ORIGIN,
	PKG_NAME,
	PKG_VERSION,
	PKG_COMMENT,
	PKG_DESC,
	PKG_MTREE,
	PKG_MESSAGE
} pkg_attr;

typedef enum {
	PKG_SCRIPT_PRE_INSTALL = 0,
	PKG_SCRIPT_POST_INSTALL,
	PKG_SCRIPT_PRE_DEINSTALL,
	PKG_SCRIPT_POST_DEINSTALL,
	PKG_SCRIPT_PRE_UPGRADE,
	PKG_SCRIPT_POST_UPGRADE,
	PKG_SCRIPT_INSTALL,
	PKG_SCRIPT_DEINSTALL,
	PKG_SCRIPT_UPGRADE
} pkg_script_t;

typedef enum {
	PKG_EXEC = 0,
	PKG_UNEXEC
} pkg_exec_t;

/* pkg */
int pkg_new(struct pkg **);
int pkg_open(const char *, struct pkg **, int);
pkg_t pkg_type(struct pkg *);
void pkg_reset(struct pkg *);
void pkg_free(struct pkg *);
const char *pkg_get(struct pkg *, pkg_attr);
struct pkg ** pkg_deps(struct pkg *);
struct pkg ** pkg_rdeps(struct pkg *);
struct pkg_file ** pkg_files(struct pkg *);
struct pkg_conflict ** pkg_conflicts(struct pkg *);
struct pkg_script ** pkg_scripts(struct pkg *);
struct pkg_exec ** pkg_execs(struct pkg *);
int pkg_resolvdeps(struct pkg *, struct pkgdb *db);

/* pkg setters */
int pkg_set(struct pkg *, pkg_attr, const char *);
int pkg_set_from_file(struct pkg *, pkg_attr, const char *);
int pkg_adddep(struct pkg *, const char *, const char *, const char *);
int pkg_addfile(struct pkg *, const char *, const char *);
int pkg_addconflict(struct pkg *, const char *);
int pkg_addexec(struct pkg *, const char *, pkg_exec_t);
int pkg_addscript(struct pkg *, const char *);

/* pkg_manifest */
int pkg_parse_manifest(struct pkg *, char *);

/* pkg_file */
int pkg_file_new(struct pkg_file **);
void pkg_file_reset(struct pkg_file *);
void pkg_file_free(struct pkg_file *);
const char * pkg_file_path(struct pkg_file *);
const char * pkg_file_sha256(struct pkg_file *);

/* pkg_conflict */
int pkg_conflict_new(struct pkg_conflict **);
void pkg_conflict_reset(struct pkg_conflict *);
void pkg_conflict_free(struct pkg_conflict *);
const char * pkg_conflict_glob(struct pkg_conflict *);

/* pkg_exec */
int pkg_script_new(struct pkg_script **);
void pkg_script_reset(struct pkg_script *);
void pkg_script_free(struct pkg_script *);
const char *pkg_script_data(struct pkg_script *);
pkg_script_t pkg_script_type(struct pkg_script *);

int pkg_exec_new(struct pkg_exec **);
void pkg_exec_reset(struct pkg_exec *);
void pkg_exec_free(struct pkg_exec *);
const char *pkg_exec_cmd(struct pkg_exec *);
pkg_exec_t pkg_exec_type(struct pkg_exec *);

/* pkgdb */
int pkgdb_open(struct pkgdb **);
void pkgdb_close(struct pkgdb *);

int pkgdb_register_pkg(struct pkgdb *, struct pkg *);
int pkgdb_unregister_pkg(struct pkgdb *, const char *);

struct pkgdb_it * pkgdb_query(struct pkgdb *, const char *, match_t);
struct pkgdb_it * pkgdb_query_which(struct pkgdb *, const char *);
struct pkgdb_it * pkgdb_query_dep(struct pkgdb *, const char *);
struct pkgdb_it * pkgdb_query_rdep(struct pkgdb *, const char *);
struct pkgdb_it * pkgdb_query_conflicts(struct pkgdb *, const char *);
struct pkgdb_it * pkgdb_query_files(struct pkgdb *, const char *);

#define PKG_BASIC 0
#define PKG_DEPS (1<<0)
#define PKG_RDEPS (1<<1)
#define PKG_CONFLICTS (1<<2)
#define PKG_FILES (1<<3)
#define PKG_ALL PKG_BASIC|PKG_DEPS|PKG_RDEPS|PKG_CONFLICTS|PKG_FILES

int pkgdb_it_next_pkg(struct pkgdb_it *, struct pkg **, int);
int pkgdb_it_next_conflict(struct pkgdb_it *, struct pkg_conflict **);
int pkgdb_it_next_file(struct pkgdb_it *, struct pkg_file **);
void pkgdb_it_free(struct pkgdb_it *);

const char *pkgdb_get_dir(void);
void pkgdb_warn(struct pkgdb *);
int pkgdb_errnum(struct pkgdb *);

/* create */
typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;
int pkg_create(const char *, pkg_formats, const char *, const char *, struct pkg *);

/* delete */
int pkg_delete(struct pkg *, struct pkgdb *, int);

/* version */
int pkg_version_cmp(const char *, const char *);

/* glue to deal with ports */
int ports_parse_plist(struct pkg *, char *, const char *);
int ports_parse_depends(struct pkg *, char *);
int ports_parse_conflicts(struct pkg *, char *);
int ports_parse_scripts(struct pkg *, char *);

#endif
