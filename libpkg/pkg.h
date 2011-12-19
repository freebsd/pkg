#ifndef _PKG_H
#define _PKG_H

#include <sys/types.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/sbuf.h>
#include <openssl/pem.h>

struct pkg;
struct pkg_dep;
struct pkg_file;
struct pkg_dir;
struct pkg_category;
struct pkg_conflict;
struct pkg_script;
struct pkg_option;
struct pkg_license;
struct pkg_user;
struct pkg_group;

struct pkgdb;
struct pkgdb_it;

struct pkg_jobs;

struct pkg_repos;
struct pkg_repos_entry;

struct pkg_config_kv;

typedef enum {
	/**
	 * The license logic is OR (dual in the ports)
	 */
	LICENSE_OR='|',
	/**
	 * The license logic is AND (multi in the ports)
	 */
	LICENSE_AND='&',
	/**
	 * The license logic un single (default in the ports)
	 */
	LICENSE_SINGLE=1
} lic_t;

typedef enum {
	PKGDB_DEFAULT=0,
	PKGDB_REMOTE
} pkgdb_t;

/**
 * Specify how an argument should be used by query functions.
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
 * Specify on which field the pattern will be matched uppon.
 */

typedef enum {
	FIELD_NONE,
	FIELD_ORIGIN,
	FIELD_NAME,
	FIELD_NAMEVER,
	FIELD_COMMENT,
	FIELD_DESC
} pkgdb_field;

/**
 * The type of package.
 */
typedef enum {
	/**
	 * The pkg type can not be determined.
	 */
	PKG_NONE = 0,

	/**
	 * The pkg refers to a local file archive.
	 */
	PKG_FILE = 1 << 0,
	/**
	 * The pkg refers to a package available on the remote repository.
	 * @todo Document which attributes are available.
	 */
	PKG_REMOTE = 1 << 1,
	/**
	 * The pkg refers to a localy installed package.
	 */
	PKG_INSTALLED = 1 << 2,
} pkg_t;

/**
 * Contains keys to refer to a string attribute.
 * Used by pkg_get() and pkg_set()
 */
typedef enum {
	PKG_ORIGIN = 0,
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
	PKG_REPOPATH,
	PKG_CKSUM,
	PKG_NEWVERSION,
	PKG_REPONAME,
	PKG_REPOURL, /* end of fields */
	PKG_FLATSIZE,
	PKG_NEW_FLATSIZE,
	PKG_NEW_PKGSIZE,
	PKG_LICENSE_LOGIC,
	PKG_AUTOMATIC,
	PKG_ROWID,
} pkg_attr;

/**
 * contains keys to refer to a string attribute
 * Used by pkg_dep_get()
 */
typedef enum {
	PKG_DEP_NAME = 0,
	PKG_DEP_ORIGIN,
	PKG_DEP_VERSION
} pkg_dep_attr;

typedef enum {
	PKG_FILE_PATH = 0,
	PKG_FILE_SUM,
	PKG_FILE_UNAME,
	PKG_FILE_GNAME
} pkg_file_attr;

typedef enum {
	PKG_DEPS = 0,
	PKG_RDEPS,
	PKG_LICENSES,
	PKG_OPTIONS,
	PKG_CATEGORIES,
	PKG_FILES,
	PKG_DIRS,
	PKG_USERS,
	PKG_GROUPS,
	PKG_CONFLICTS,
	PKG_SCRIPTS
} pkg_list;

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

typedef enum _pkg_jobs_t {
	PKG_JOBS_INSTALL,
	PKG_JOBS_DEINSTALL,
	PKG_JOBS_UPGRADE
} pkg_jobs_t;

typedef enum _pkg_config_key {
	PKG_CONFIG_REPO = 0,
	PKG_CONFIG_DBDIR = 1,
	PKG_CONFIG_CACHEDIR = 2,
	PKG_CONFIG_PORTSDIR = 3,
	PKG_CONFIG_REPOKEY = 4,
	PKG_CONFIG_MULTIREPOS = 5,
	PKG_CONFIG_HANDLE_RC_SCRIPTS = 6,
	PKG_CONFIG_ASSUME_ALWAYS_YES = 7,
} pkg_config_key;

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
} pkg_error_t;

/**
 * Allocate a new pkg.
 * Allocated pkg must be deallocated by pkg_free().
 */
int pkg_new(struct pkg **, pkg_t type);

/**
 * Reset a pkg to its initial state.
 * Useful to avoid sequences of pkg_new() and pkg_free().
 */
void pkg_reset(struct pkg *, pkg_t type);

/**
 * Deallocate a pkg
 */
void pkg_free(struct pkg *);

/**
 * Check if a package is valid according to its type.
 */
int pkg_is_valid(struct pkg *);

/**
 * Open a package file archive and retrive informations.
 * @param p A pointer to pkg allocated by pkg_new(), or if it points to a
 * NULL pointer, the function allocate a new pkg using pkg_new().
 * @param path The path to the local package archive.
 */
int pkg_open(struct pkg **p, const char *path, struct sbuf *mbuf);

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
int pkg_get2(struct pkg const *const, ...);
#define pkg_get(pkg, ...) pkg_get2(pkg, __VA_ARGS__, -1)

int pkg_list_is_empty(struct pkg *, pkg_list);
/**
 * Iterates over the dependencies of the package.
 * @param dep Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_deps(struct pkg *, struct pkg_dep **dep);

/**
 * Iterates over the reverse dependencies of the package.
 * That is, the packages which require this package.
 * @param dep Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_rdeps(struct pkg *, struct pkg_dep **dep);

/**
 * Iterates over the files of the package.
 * @param file Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_files(struct pkg *, struct pkg_file **file);

/**
 * Iterates over the directories of the package.
 * @param Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_dirs(struct pkg *pkg, struct pkg_dir **dir);

/**
 * Iterates over the categories of the package.
 * @param Must be set to NULL for the first call.
 * @return An error code
 */
int pkg_categories(struct pkg *pkg, struct pkg_category **category);

/**
 * Iterates over the licenses of the package.
 * @param Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_licenses(struct pkg *pkg, struct pkg_license **license);

/**
 * Iterates over the users of the package.
 * @param Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_users(struct pkg *pkg, struct pkg_user **user);

/**
 * Iterates over the groups of the package.
 * @param Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_groups(struct pkg *pkg, struct pkg_group **group);

/**
 * Iterates over the conflicts of the package.
 * @param conflict Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_conflicts(struct pkg *, struct pkg_conflict **conflict);

/**
 * Iterates over the scripts of the package.
 * @param script Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_scripts(struct pkg *, struct pkg_script **script);

/**
 * Iterates over the options of the package.
 * @param  option Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_options(struct pkg *, struct pkg_option **option);

/**
 * @todo Document
 */
int pkg_analyse_files(struct pkgdb *, struct pkg *);

/**
 * Generic setter for simple attributes.
 */
int pkg_set2(struct pkg *pkg, ...);
#define pkg_set(pkg, ...) pkg_set2(pkg, __VA_ARGS__, -1)

/*int pkg_set(struct pkg *pkg, pkg_attr attr, const char *value);*/

/**
 * Read the content of a file into a buffer, then call pkg_set().
 */
int pkg_set_from_file(struct pkg *pkg, pkg_attr attr, const char *file);

/**
 * Allocate a new struct pkg and add it to the deps of pkg.
 * @return An error code.
 */
int pkg_adddep(struct pkg *pkg, const char *name, const char *origin, const
			   char *version);
int pkg_addrdep(struct pkg *pkg, const char *name, const char *origin, const
			   char *version);

//int pkg_addfile(struct pkg *, const char *fmt);
/**
 * Allocate a new struct pkg_file and add it to the files of pkg.
 * @param sha256 The ascii representation of the sha256 or a NULL pointer.
 * @return An error code.
 */
int pkg_addfile(struct pkg *pkg, const char *path, const char *sha256);

/**
 * Allocate a new struct pkg_file and add it to the files of pkg;
 * @param path path of the file
 * @param sha256 The ascii representation of the sha256 or a NULL pointer.
 * @param uname the user name of the file
 * @param gname the group name of the file
 * @param perm the permission
 * @return An error code.
 */
int pkg_addfile_attr(struct pkg *pkg, const char *path, const char *sha256, const char *uname, const char *gname, mode_t perm);

/**
 * Add a path
 * @return An error code.
 */
int pkg_adddir(struct pkg *pkg, const char *path, int try);

/**
 * Allocate a new struct pkg_file and add it to the files of pkg;
 * @param path path of the file
 * @param uname the user name of the file
 * @param gname the group name of the file
 * @param perm the permission
 * @return An error code.
 */
int pkg_adddir_attr(struct pkg *pkg, const char *path, const char *uname, const char *gname, mode_t perm, int try);

/**
 * Add a category
 * @return An error code.
 */
int pkg_addcategory(struct pkg *pkg, const char *name);

/**
 * Add a license
 * @return An error code.
 */
int pkg_addlicense(struct pkg *pkg, const char *name);

/**
 * Add a user
 * @return An error code.
 */
int pkg_adduser(struct pkg *pkg, const char *name);

/**
 * Add a group
 * @return An error code.
 */
int pkg_addgroup(struct pkg *pkg, const char *group);

/**
 * Add a user
 * @return An error code.
 */
int pkg_adduid(struct pkg *pkg, const char *name, const char *uidstr);

/**
 * Add a gid
 * @return an error code
 */
int pkg_addgid(struct pkg *pkg, const char *group, const char *gidstr);

/**
 * Allocate a new struct pkg_conflict and add it to the conflicts of pkg.
 * @return An error code.
 */
int pkg_addconflict(struct pkg *pkg, const char *glob);

/**
 * Allocate a new struct pkg_script and add it to the scripts of pkg.
 * @param path The path to the script on disk.
 @ @return An error code.
 */
int pkg_addscript(struct pkg *pkg, const char *data, pkg_script_t type);

/**
 * Helper which call pkg_addscript() with the content of the file and
 * with the correct type.
 */
int pkg_addscript_file(struct pkg *pkg, const char *path);
int pkg_appendscript(struct pkg *pkg, const char *cmd, pkg_script_t type);

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
int pkg_load_manifest_file(struct pkg *pkg, const char *fpath);

/**
 * Emit a manifest according to the attributes of pkg.
 * @param buf A pointer which will hold the allocated buffer containing the
 * manifest. To be free'ed.
 * @return An error code.
 */
int pkg_emit_manifest(struct pkg *pkg, char **buf);

/* pkg_dep */
const char *pkg_dep_get(struct pkg_dep const * const , const pkg_dep_attr);

/* pkg_file */
const char *pkg_file_get(struct pkg_file const * const, const pkg_file_attr);

const char *pkg_dir_path(struct pkg_dir *);
int pkg_dir_try(struct pkg_dir *);

const char *pkg_category_name(struct pkg_category *);

const char *pkg_license_name(struct pkg_license *);

const char *pkg_user_name(struct pkg_user *);
const char *pkg_user_uidstr(struct pkg_user *);
const char *pkg_group_name(struct pkg_group *);
const char *pkg_group_gidstr(struct pkg_group *);

/* pkg_conflict */
const char * pkg_conflict_glob(struct pkg_conflict *);

/* pkg_script */
const char *pkg_script_data(struct pkg_script *);
pkg_script_t pkg_script_type(struct pkg_script *);

/* pkg_option */
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
int pkgdb_open(struct pkgdb **db, pkgdb_t type);

/**
 * Close and free the struct pkgdb.
 */
void pkgdb_close(struct pkgdb *db);

/** 
 * Dump the content of the database in yaml format
 * only to use when mtree will be deprecated
 */

int pkgdb_dump(struct pkgdb *db, char *dest);
int pkgdb_load(struct pkgdb *db, char *dest);

/**
 * Whether a package database instance has a particular flag.
 * @return 0 if false, true otherwise
 */
int pkgdb_has_flag(struct pkgdb *db, int flag);

/* The flags used in pkgdb_has_flag() */
#define	PKGDB_FLAG_IN_FLIGHT	(1 << 0)

/**
 * register a package to the database.
 * @return An error code.
 */
int pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg, int complete);

/**
 * Complete an in-flight package registration command.
 */
int pkgdb_register_finale(struct pkgdb *db, int retcode);

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
struct pkgdb_it * pkgdb_rquery(struct pkgdb *db, const char *pattern,
		match_t type, unsigned int field, const char *reponame);

/**
 * 
 */
struct pkgdb_it *pkgdb_query_installs(struct pkgdb *db, match_t type, int nbpkgs, char **pkgs, const char *reponame);
struct pkgdb_it *pkgdb_query_upgrades(struct pkgdb *db, const char *reponame);
struct pkgdb_it *pkgdb_query_downgrades(struct pkgdb *db, const char *reponame);
struct pkgdb_it *pkgdb_query_delete(struct pkgdb *db, match_t type, int nbpkgs, char **pkgs, int recursive);
struct pkgdb_it *pkgdb_query_autoremove(struct pkgdb *db);

/**
 * @todo Return directly the struct pkg?
 */
struct pkgdb_it * pkgdb_query_which(struct pkgdb *db, const char *path);

#define PKG_LOAD_BASIC 0
#define PKG_LOAD_DEPS (1<<0)
#define PKG_LOAD_RDEPS (1<<1)
#define PKG_LOAD_CONFLICTS (1<<2)
#define PKG_LOAD_FILES (1<<3)
#define PKG_LOAD_SCRIPTS (1<<5)
#define PKG_LOAD_OPTIONS (1<<6)
#define PKG_LOAD_MTREE (1<<7)
#define PKG_LOAD_DIRS (1<<8)
#define PKG_LOAD_CATEGORIES (1<<9)
#define PKG_LOAD_LICENSES (1<<10)
#define PKG_LOAD_USERS (1<<11)
#define PKG_LOAD_GROUPS (1<<12)

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

int pkgdb_load_deps(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_rdeps(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_conflicts(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_files(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_dirs(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_scripts(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_options(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_mtree(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_category(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_license(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_user(struct pkgdb *db, struct pkg *pkg);
int pkgdb_load_group(struct pkgdb *db, struct pkg *pkg);

/**
 * Compact the database to save space.
 * Note that the function will really compact the database only if some
 * internal criterias are met.
 * @return An error code.
 */
int pkgdb_compact(struct pkgdb *db);

/**
 * Install and register a new package.
 * @param db An opened pkgdb
 * @param path The path to the package archive file on the local disk
 * @return An error code.
 */
int pkg_add(struct pkgdb *db, const char *path, int flags);

#define PKG_ADD_UPGRADE (1 << 0)
#define PKG_ADD_UPGRADE_NEW (1 << 1)
#define PKG_ADD_AUTOMATIC (1 << 2)

/**
 * Allocate a new pkg_jobs.
 * @param db A pkgdb open with PKGDB_REMOTE.
 * @return An error code.
 */
int pkg_jobs_new(struct pkg_jobs **jobs, pkg_jobs_t type, struct pkgdb *db);

/**
 * Free a pkg_jobs
 */
void pkg_jobs_free(struct pkg_jobs *jobs);

/**
 * Add a pkg to the jobs queue.
 * @return An error code.
 */
int pkg_jobs_add(struct pkg_jobs *jobs, struct pkg *pkg);

/**
 * Returns true if there are no jobs.
 */
int pkg_jobs_is_empty(struct pkg_jobs *jobs);

/**
 * Iterates over the packages in the jobs queue.
 * @param pkg Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_jobs(struct pkg_jobs *jobs, struct pkg **pkg);

/**
 * Apply the jobs in the queue (fetch and install).
 * @return An error code.
 */
int pkg_jobs_apply(struct pkg_jobs *jobs, int force);

/**
 * Archive formats options.
 */
typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;

/**
 * Create package from an installed & registered package
 */
int pkg_create_installed(const char *, pkg_formats, const char *, struct pkg *);

/**
 * Create package from fakeroot install with a metadata directory
 */
int pkg_create_fakeroot(const char *, pkg_formats, const char *, const char *);

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
int pkg_repo_verify(const char *path, unsigned char *sig, unsigned int sig_len);

/**
 * Get the value of a configuration key
 */
int pkg_config_string(pkg_config_key key, const char **value);
int pkg_config_bool(pkg_config_key key, bool *value);
int pkg_config_list(pkg_config_key key, struct pkg_config_kv **kv);

/**
 * @todo Document
 */
int pkg_version_cmp(const char * const , const char * const);

/**
 * Fetch a file.
 * @return An error code.
 */
int pkg_fetch_file(const char *url, const char *dest);

/* glue to deal with ports */
int ports_parse_plist(struct pkg *, char *);

/**
 * Creates a new repository object
 * This function is used for creating a repository
 * object that can later be used by pkg_repos_load()
 * for loading the repositories from file and pkg_repos_next()
 * for iterating over the repositories tail.
 * @return EPKG_OK on success and EPKG_FATAL on error
 */
int pkg_repos_new(struct pkg_repos **repos);

/**
 * Loads the remote repositories from file
 * @param repos A valid repository object as received from pkg_repos_new()
 * @return EPKG_OK on success and EPKG_FATAL on error
 */
int pkg_repos_load(struct pkg_repos *repos);

/**
 * Adds a repository entry found from the repositories file to the tail
 * @param repos A valid repository object as returned by pkg_repos_new()
 * @param re A valid repository entry object
 * @return EPKG_OK on success and EPKG_FATAL on error
 */
int pkg_repos_add(struct pkg_repos *repos, struct pkg_repos_entry *re);

/**
 * Get the next repository from the configuration file
 * @param repos A valid repository object as returned by pkg_repos_new()
 * @param re A pointer to a repository entry to save the result. Must be set to
 * NULL for the first repository entry
 * @return EPKG_OK on success and EPKG_END if end of tail is reached
 */
int pkg_repos_next(struct pkg_repos *repos, struct pkg_repos_entry **re);

/**
 * Switches to a single repository while running in multi-repos mode
 * @param repos A valid repository object as returned by pkg_repos_new()
 * @param reponame The name of the repository to switch to
 * @return EPKG_OK if switching to reponame was successful and EPKG_FATAL
 * in case of error, e.g. repository does not exists
 */
int pkg_repos_switch(struct pkg_repos *repos, const char *reponame);

/**
 * Switches back to multi-repos mode and resets any switchable repos
 * @param repos A valid repository object as returned by pkg_repos_new()
 * @return EPKG_OK on success
 */
int pkg_repos_switch_reset(struct pkg_repos *repos);

/**
 * Frees the memory used by the repository objects
 * @param repos A valid repository object as returned by pkg_repos_new()
 */
void pkg_repos_free(struct pkg_repos *repos);

/**
 * Check if an attached repository exists
 * @param repos A valid repository object as returned by pkg_repos_new()
 * @param reponame The name of the repository to be checked
 * @return EPKG_OK if repository exists and EPKG_FATAL otherwise
 */
int pkg_repos_exists(struct pkg_repos *repos, const char *reponame);

/**
 * Returns the name associated with a repository entry
 * @param re A valid repository entry object
 */
const char * pkg_repos_get_name(struct pkg_repos_entry *re);

/**
 * Returns the URL associated wth a repository entry
 * @param re A valid repository entry object
 */
const char * pkg_repos_get_url(struct pkg_repos_entry *re);

/**
 * Returns the line from the configuration file where a repository is defined
 * @param re A valid repository entry object
 */
unsigned int pkg_repos_get_line(struct pkg_repos_entry *re);

/**
 * Returns the next database, which is ATTACH'ed to the main one
 * @param it A valid pkgdb_it object as received from
 * pkgdb_repos_new() call
 * @return A string containing the next database attached
 * to the main one, or NULL if end of list is reached.
 */
const char * pkgdb_repos_next(struct pkgdb_it *it);

/**
 * Adds the URL associated with a repository to a package object
 * @param pkg A valid package object
 * @param reponame The name of the repository (attached database)
 * @return EPKG_OK on success and EPKG_FATAL on error
 */
int pkg_add_repo_url(struct pkg *pkg, const char *reponame);

/**
 * @todo Document
 */
int pkg_copy_tree(struct pkg *, const char *src, const char *dest);

/**
 * scripts handling
 */
int pkg_script_pre_install(struct pkg *);
int pkg_script_post_install(struct pkg *);
int pkg_script_pre_upgrade(struct pkg *);
int pkg_script_post_upgrade(struct pkg *);
int pkg_script_pre_deinstall(struct pkg *);
int pkg_script_post_deinstall(struct pkg *);
int pkg_script_run(struct pkg *, pkg_script_t type);

/**
 * Event type used to report progress or problems.
 */
typedef enum {
	/* informational */
	PKG_EVENT_INSTALL_BEGIN = 0,
	PKG_EVENT_INSTALL_FINISHED,
	PKG_EVENT_DEINSTALL_BEGIN,
	PKG_EVENT_DEINSTALL_FINISHED,
	PKG_EVENT_UPGRADE_BEGIN,
	PKG_EVENT_UPGRADE_FINISHED,
	PKG_EVENT_FETCHING,
	PKG_EVENT_INTEGRITYCHECK_BEGIN,
	PKG_EVENT_INTEGRITYCHECK_FINISHED,
	/* errors */
	PKG_EVENT_ERROR,
	PKG_EVENT_ERRNO,
	PKG_EVENT_ARCHIVE_COMP_UNSUP = 65536,
	PKG_EVENT_ALREADY_INSTALLED,
	PKG_EVENT_FAILED_CKSUM,
	PKG_EVENT_CREATE_DB_ERROR,
	PKG_EVENT_REQUIRED,
	PKG_EVENT_MISSING_DEP,
} pkg_event_t;

struct pkg_event {
	pkg_event_t type;
	union {
		struct {
			const char *func;
			const char *arg;
		} e_errno;
		struct {
			char *msg;
		} e_pkg_error;
		struct {
			const char *url;
			off_t total;
			off_t done;
			time_t elapsed;
		} e_fetching;
		struct {
			struct pkg *pkg;
		} e_already_installed;
		struct {
			struct pkg *pkg;
		} e_install_begin;
		struct {
			struct pkg *pkg;
		} e_install_finished;
		struct {
			struct pkg *pkg;
		} e_deinstall_begin;
		struct {
			struct pkg *pkg;
		} e_deinstall_finished;
		struct {
			struct pkg *pkg;
		} e_upgrade_begin;
		struct {
			struct pkg *pkg;
		} e_upgrade_finished;
		struct {
			struct pkg *pkg;
			struct pkg_dep *dep;
		} e_missing_dep;
		struct {
			struct pkg *pkg;
			int force;
		} e_required;
	};
};

/**
 * Event callback mechanism.  Events will be reported using this callback,
 * providing an event identifier and up to two event-specific pointers.
 */
typedef int(*pkg_event_cb)(void *, struct pkg_event *);

void pkg_event_register(pkg_event_cb cb, void *data);

int pkg_stop_rc_scripts(struct pkg *);
int pkg_start_rc_scripts(struct pkg *);


int pkg_init(const char *);
int pkg_shutdown(void);

int pkg_add_user_group(struct pkg *pkg);
int pkg_delete_user_group(struct pkgdb *db, struct pkg *pkg);

#endif
