#ifndef _PKG_H
#define _PKG_H

#include <sys/types.h>

struct pkg;
struct pkg_file;
struct pkg_conflict;
struct pkg_exec;
struct pkg_script;

struct pkgdb;
struct pkgdb_it;

/**
 * Specify how an argument should be used by matching functions.
 */
typedef enum {
	/**
	 * The argument does not matter, all items will be matched.
	 */
	MATCH_ALL,
	/**
	 * The argument is the exact pattern.
	 */
	MATCH_EXACT,
	/**
	 * The argument is a globbing expression.
	 */
	MATCH_GLOB,
	/**
	 * The argument is a basic regular expression.
	 */
	MATCH_REGEX,
	/**
	 * The argument is an extended regular expression.
	 */
	MATCH_EREGEX
} match_t;

/**
 * The type of package.
 */
typedef enum {
	/**
	 * The pkg refers to a local file archive.
	 */
	PKG_FILE,
	/**
	 * The pkg refers to a package available of the remote repository.
	 * @todo Document which attributes are available.
	 */
	PKG_REMOTE,
	/**
	 * The pkg refers to a localy installed package.
	 */
	PKG_INSTALLED,
	/**
	 * The pkg refers to a non installed package.
	 * @warning That means that the pkg contains only few attributes:
	 *   - origin
	 *   - name
	 *   - version
	 */
	PKG_NOTFOUND,
	/**
	 * The pkg type can not be determined.
	 */
	PKG_NONE,
} pkg_t;

/**
 * Contains keys to refer to a string attribute.
 * Used by pkg_get() and pkg_set()
 */
typedef enum {
	PKG_ORIGIN,
	PKG_NAME,
	PKG_VERSION,
	PKG_COMMENT,
	PKG_DESC,
	PKG_MTREE,
	PKG_MESSAGE,
	PKG_ARCH,
	PKG_OSVERSION,
	PKG_MAINTAINER,
	PKG_WWW,
	PKG_PREFIX
} pkg_attr;

/**
 * Determine the type of a pkg_script.
 */
typedef enum _pkg_script_t {
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

/**
 * Determine the type of a pkg_exec.
 * @warning Legacy interface, may be removed later.
 */
typedef enum {
	PKG_EXEC = 0,
	PKG_UNEXEC
} pkg_exec_t;

/**
 * Error type used everywhere by libpkg.
 */
typedef enum {
	EPKG_OK = 0,
	/**
	 * No more items available (end of the loop).
	 */
	EPKG_END,
	EPKG_WARNING,
	/**
	 * The function encountered a fatal error.
	 * errno was set to describe the error.
	 */
	EPKG_FATAL,
	EPKG_REQUIRED,
	EPKG_NOT_ORIGIN,
	EPKG_NOT_NAME,
} pkg_error_t;

/**
 * Allocate a new pkg.
 * Allocated pkg must be deallocated by pkg_free().
 */
int pkg_new(struct pkg **);

/**
 * Reset a pkg to its initial state.
 * Useful to avoid sequences of pkg_new() and pkg_free().
 */
void pkg_reset(struct pkg *);

/**
 * Deallocate a pkg
 */
void pkg_free(struct pkg *);

/**
 * Open a file archive.
 * @param path A local path or an URL.
 * @param p A pointer to pkg allocated by pkg_new(), or if it points to a
 * NULL pointer, the function allocate a new pkg using pkg_new().
 * @param flags Unused.
 */
int pkg_open(const char *path, struct pkg **p, int flags);

pkg_t pkg_type(struct pkg *);

/**
 * Generic getter for simple attributes.
 * @return NULL-terminated string.
 * @warning May return a NULL pointer.
 */
const char *pkg_get(struct pkg *, pkg_attr);

/**
 * Returns the size of the uncompressed package.
 */
int64_t pkg_flatsize(struct pkg *);

/**
 * @return NULL-terminated array of pkg.
 * @warning May return a NULL pointer.
 */
struct pkg ** pkg_deps(struct pkg *);

/**
 * Returns the reverse dependencies.
 * That is, the packages which require this package.
 * @return NULL-terminated array of pkg.
 * @warning May return a NULL pointer.
 */
struct pkg ** pkg_rdeps(struct pkg *);

/**
 * @return NULL-terminated array of pkg_file.
 * @warning May return a NULL pointer.
 */
struct pkg_file ** pkg_files(struct pkg *);

/**
 * @return NULL-terminated array of pkg_conflict.
 * @warning May return a NULL pointer.
 */
struct pkg_conflict ** pkg_conflicts(struct pkg *);

/**
 * @return NULL-terminated array of pkg_script.
 * @warning May return a NULL pointer.
 */
struct pkg_script ** pkg_scripts(struct pkg *);

/**
 * @return NULL-terminated array of pkg_exec.
 * @warning May return a NULL pointer.
 * @warning Legacy interface, may be removed later.
 */
struct pkg_exec ** pkg_execs(struct pkg *);

/**
 * @return NULL-terminated array of pkg_option
 * @warning May return a NULL pointer.
 */
struct pkg_option ** pkg_options(struct pkg *);

/**
 * Resolve the dependencies of the package.
 * Check if the dependencies are registered into the local database and set
 * their type to #PKG_INSTALLED or #PKG_NOTFOUND.
 */
int pkg_resolvdeps(struct pkg *, struct pkgdb *db);

/**
 * @todo Document
 */
int pkg_analyse_files(struct pkgdb *, struct pkg *);

/**
 * add a new package this will check if upgrade is needed
 */
int pkg_add(struct pkgdb *, struct pkg *);

/**
 * Generic setter for simple attributes.
 */
int pkg_set(struct pkg *, pkg_attr, const char *);

/**
 * Read the content of a file into a buffer, then call pkg_set().
 */
int pkg_set_from_file(struct pkg *, pkg_attr, const char *);

/**
 * Set the uncompressed size of the package.
 */
int pkg_setflatsize(struct pkg *, int64_t);

int pkg_adddep(struct pkg *, const char *, const char *, const char *);
int pkg_addfile(struct pkg *, const char *, const char *);
int pkg_addconflict(struct pkg *, const char *);
int pkg_addexec(struct pkg *, const char *, pkg_exec_t);
int pkg_addscript(struct pkg *, const char *);
int pkg_addoption(struct pkg *, const char *, const char *);

/* pkg_manifest */
int pkg_parse_manifest(struct pkg *, char *);
int pkg_emit_manifest(struct pkg *, char **);

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

/* pkg_option */
int pkg_option_new(struct pkg_option **);
void pkg_option_reset(struct pkg_option *);
void pkg_option_free(struct pkg_option *);
const char *pkg_option_opt(struct pkg_option *);
const char *pkg_option_value(struct pkg_option *);

/* pkg_repo */
int pkg_create_repo(char *, void (*)(struct pkg *, void *), void *);

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
struct pkgdb_it * pkgdb_query_execs(struct pkgdb *, const char *);
struct pkgdb_it * pkgdb_query_scripts(struct pkgdb *, const char *);
struct pkgdb_it * pkgdb_query_options(struct pkgdb *, const char *);

#define PKG_BASIC 0
#define PKG_DEPS (1<<0)
#define PKG_RDEPS (1<<1)
#define PKG_CONFLICTS (1<<2)
#define PKG_FILES (1<<3)
#define PKG_EXECS (1<<4)
#define PKG_SCRIPTS (1<<5)
#define PKG_OPTIONS (1<<6)
#define PKG_ALL PKG_BASIC|PKG_DEPS|PKG_RDEPS|PKG_CONFLICTS|PKG_FILES|PKG_EXECS|PKG_SCRIPTS|PKG_OPTIONS

int pkgdb_it_next_pkg(struct pkgdb_it *, struct pkg **, int);
int pkgdb_it_next_conflict(struct pkgdb_it *, struct pkg_conflict **);
int pkgdb_it_next_file(struct pkgdb_it *, struct pkg_file **);
int pkgdb_it_next_exec(struct pkgdb_it *, struct pkg_exec **);
int pkgdb_it_next_script(struct pkgdb_it *, struct pkg_script **);
int pkgdb_it_next_option(struct pkgdb_it *, struct pkg_option **);
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
int ports_parse_plist(struct pkg *, char *);
int ports_parse_depends(struct pkg *, char *);
int ports_parse_conflicts(struct pkg *, char *);
int ports_parse_scripts(struct pkg *, char *);
int ports_parse_options(struct pkg *, char *);

pkg_error_t pkg_error_number(void);
const char * pkg_error_string(void);
void pkg_error_warn(const char *fmt, ...);

#endif
