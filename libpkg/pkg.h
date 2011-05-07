#ifndef _PKG_H
#define _PKG_H

#include <sys/types.h>
#include <openssl/pem.h>

struct pkg;
struct pkg_file;
struct pkg_conflict;
struct pkg_exec;
struct pkg_script;

struct pkgdb;
struct pkgdb_it;

typedef enum {
	PKGDB_DEFAULT=0,
	PKGDB_REMOTE
} pkgdb_t;

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
	 * The pkg refers to a package available on the remote repository.
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
	PKG_NONE
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
	PKG_PREFIX,
	PKG_NEWVERSION,
	PKG_NEWPATH
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
	EPKG_WARN,
	/**
	 * The function encountered a fatal error.
	 */
	EPKG_FATAL,
	/**
	 * Can not delete the package because it is required by another package.
	 */
	EPKG_REQUIRED,
	/**
	 * Can not install the package because it is already installed.
	 */
	EPKG_INSTALLED,
	/**
	 * Can not install the package because some dependencies are unresolved.
	 */
	EPKG_DEPENDENCY,
	/**
	 * the format requested is not supported 
	 */
	EPKG_FORMAT,
	EPKG_NOT_ORIGIN,
	EPKG_NOT_NAME
} pkg_error_t;

#define PKG_LT -1
#define PKG_EQ 0
#define PKG_GT 1

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
 * Open a package file archive and retrive informations.
 * @param p A pointer to pkg allocated by pkg_new(), or if it points to a
 * NULL pointer, the function allocate a new pkg using pkg_new().
 * @param path The path to the local package archive.
 */
int pkg_open(struct pkg **p, const char *path);


/**
 * @return the type of the package.
 * @warning returns PKG_NONE on error.
 */
pkg_t pkg_type(struct pkg const * const);

/**
 * Generic getter for simple attributes.
 * @return NULL-terminated string.
 * @warning May return a NULL pointer.
 */
const char *pkg_get(struct pkg const * const , const pkg_attr);

/**
 * @return the size of the uncompressed package.
 */
int64_t pkg_flatsize(struct pkg *);

/**
 * @return the size of the uncompressed package, in its futur version.
 */
int64_t pkg_new_flatsize(struct pkg *);

/**
 * @return the size of the compressed package, in its futur version.
 */
int64_t pkg_new_pkgsize(struct pkg *);

/**
 * @return NULL-terminated array of pkg.
 */
struct pkg ** pkg_deps(struct pkg *);

/**
 * Returns the reverse dependencies.
 * That is, the packages which require this package.
 * @return NULL-terminated array of pkg.
 */
struct pkg ** pkg_rdeps(struct pkg *);

/**
 * @return NULL-terminated array of pkg_file.
 */
struct pkg_file ** pkg_files(struct pkg *);

/**
 * @return NULL-terminated array of pkg_conflict.
 */
struct pkg_conflict ** pkg_conflicts(struct pkg *);

/**
 * @return NULL-terminated array of pkg_script.
 */
struct pkg_script ** pkg_scripts(struct pkg *);

/**
 * @return NULL-terminated array of pkg_exec.
 * @warning Legacy interface, may be removed later.
 */
struct pkg_exec ** pkg_execs(struct pkg *);

/**
 * @return NULL-terminated array of pkg_option
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
 * Generic setter for simple attributes.
 */
int pkg_set(struct pkg *pkg, pkg_attr attr, const char *value);

/**
 * Read the content of a file into a buffer, then call pkg_set().
 */
int pkg_set_from_file(struct pkg *pkg, pkg_attr attr, const char *file);

/**
 * Set the uncompressed size of the package.
 * @return An error code.
 */
int pkg_setflatsize(struct pkg *pkg, int64_t size);

/**
 * Set the uncompressed size of the package, in its futur version.
 * @return An error code.
 */
int pkg_setnewflatsize(struct pkg *pkg, int64_t size);

/**
 * Set the compressed size of the package, in its futur version.
 * @return An error code.
 */
int pkg_setnewpkgsize(struct pkg *pkg, int64_t size);



/**
 * Allocate a new struct pkg and add it to the deps of pkg.
 * @return An error code.
 */
int pkg_adddep(struct pkg *pkg, const char *name, const char *origin, const
			   char *version);

/**
 * Allocate a new struct pkg_file and add it to the files of pkg.
 * @param sha256 The ascii representation of the sha256 or a NULL pointer.
 * @return An error code.
 */
int pkg_addfile(struct pkg *pkg, const char *path, const char *sha256);

/**
 * Allocate a new struct pkg_conflict and add it to the conflicts of pkg.
 * @return An error code.
 */
int pkg_addconflict(struct pkg *pkg, const char *glob);

/**
 * Allocate a new struct pkg_exec and add it to the execs of pkg.
 * @return An error code.
 */
int pkg_addexec(struct pkg *pkg, const char *cmd, pkg_exec_t type);

/**
 * Allocate a new struct pkg_script and add it to the scripts of pkg.
 * @param path The path to the script on disk.
 @ @return An error code.
 */
int pkg_addscript(struct pkg *pkg, const char *path);

/**
 * Allocate a new struct pkg_option and add it to the options of pkg.
 * @return An error code.
 */
int pkg_addoption(struct pkg *pkg, const char *name, const char *value);

/**
 * Parse a manifest and set the attributes of pkg accordingly.
 * @param buf An NULL-terminated buffer containing the manifest data.
 * @return An error code.
 */
int pkg_parse_manifest(struct pkg *pkg, char *buf);

/**
 * Emit a manifest according to the attributes of pkg.
 * @param buf A pointer which will hold the allocated buffer containing the
 * manifest. To be free'ed.
 * @return An error code.
 */
int pkg_emit_manifest(struct pkg *pkg, char **buf);

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

/**
 * Create a repository database.
 * @param path The path where the repository live.
 * @param callback A function which is called at every step of the process.
 * @param data A pointer which is passed to the callback.
 * @param sum An 65 long char array to receive the sha256 sum
 */
int pkg_create_repo(char *path, void (*callback)(struct pkg *, void *), void *);
int pkg_finish_repo(char *patj, pem_password_cb *cb, char *rsa_key_path);

/**
 * Open the local package database.
 * The db must be free'ed with pkgdb_close().
 * @return An error code.
 */
int pkgdb_open(struct pkgdb **db, pkgdb_t remote);

/**
 * Close and free the struct pkgdb.
 */
void pkgdb_close(struct pkgdb *db);

/**
 * Register a package to the database.
 * @return An error code.
 */
int pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg);

/**
 * Unregister a package from the database.
 * @return An error code.
 */
int pkgdb_unregister_pkg(struct pkgdb *pkg, const char *origin);

/**
 * Query the local package database.
 * @param type Describe how pattern should be used.
 * @warning Returns NULL on failure.
 */
struct pkgdb_it * pkgdb_query(struct pkgdb *db, const char *pattern,
							  match_t type);

/**
 * 
 */
struct pkgdb_it *pkgdb_query_upgrades(struct pkgdb *db);
struct pkgdb_it *pkgdb_query_downgrades(struct pkgdb *db);

/**
 * @todo Return directly the struct pkg?
 */
struct pkgdb_it * pkgdb_query_which(struct pkgdb *db, const char *path);

#define PKG_LOAD_BASIC 0
#define PKG_LOAD_DEPS (1<<0)
#define PKG_LOAD_RDEPS (1<<1)
#define PKG_LOAD_CONFLICTS (1<<2)
#define PKG_LOAD_FILES (1<<3)
#define PKG_LOAD_EXECS (1<<4)
#define PKG_LOAD_SCRIPTS (1<<5)
#define PKG_LOAD_OPTIONS (1<<6)
#define PKG_LOAD_MTREE (1<<7)
#define PKG_LOAD_NEWVERSION (1<<8)

/**
 * Get the next pkg.
 * @param pkg An allocated struct pkg or a pointer to a NULL pointer. In the
 * last case, the function take care of the allocation.
 * @param flags OR'ed PKG_LOAD_*
 * @return An error code.
 */
int pkgdb_it_next(struct pkgdb_it *, struct pkg **pkg, int flags);

/**
 * Free a struct pkgdb_it.
 */
void pkgdb_it_free(struct pkgdb_it *);

int pkgdb_loaddeps(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadrdeps(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadconflicts(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadfiles(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadexecs(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadscripts(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadoptions(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadmtree(struct pkgdb *db, struct pkg *pkg);

/**
 * Compact the database to save space.
 * Note that the function will really compact the database only if some
 * internal criterias are met.
 * @return An error code.
 */
int pkgdb_compact(struct pkgdb *db);

/**
 * Install and register a new package.
 * @param path The path to the package archive file on the local disk
 * @return An error code.
 */
int pkg_add(struct pkgdb *db, const char *path, struct pkg **pkg);

/**
 * Archive formats options.
 */
typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;

/**
 * @todo Document
 */
int pkg_create(const char *, pkg_formats, const char *, const char *, struct pkg *);

/**
 * Remove and unregister the package.
 * @param force If set to one, the function will not fail if the package is
 * required by other packages.
 * @return An error code.
 */
int pkg_delete(struct pkg *pkg, struct pkgdb *db, int force);

/**
 * Get the value of a configuration key
 */
const char * pkg_config(const char *key);

/**
 * @todo Document
 */
int pkg_version_cmp(const char * const , const char * const);

/**
 * A function used as a callback by functions which fetch files from the
 * network.
 */
typedef void (*fetch_cb)(void *data, const char *url, off_t total, off_t done,
						 time_t elapsed);

/**
 * Fetch a file.
 * @return An error code.
 */
int pkg_fetch_file(const char *url, const char *dest, void *data, fetch_cb cb);
/**
 * Fetch to a given buffer
 * @return An error code
 */
int pkg_fetch_buffer(const char *url, char **buf, void *data, fetch_cb cb);

/* glue to deal with ports */
int ports_parse_plist(struct pkg *, char *);
int ports_parse_depends(struct pkg *, char *);
int ports_parse_conflicts(struct pkg *, char *);
int ports_parse_scripts(struct pkg *, char *);
int ports_parse_options(struct pkg *, char *);

/**
 * Return the last error number
 */
pkg_error_t pkg_error_number(void);

/**
 * Return the last error string
 */
const char * pkg_error_string(void);

/**
 * Behave like warn(3), but with the pkg error instead of errno
 */
void pkg_error_warn(const char *fmt, ...);

/**
 * TODO
 */
int pkg_copy_tree(struct pkg *, const char *src, const char *dest);


#endif
