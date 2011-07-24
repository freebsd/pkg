#ifndef _PKG_H
#define _PKG_H

#include <stdarg.h>
#include <sys/types.h>
#include <openssl/pem.h>

struct pkg;
struct pkg_dep;
struct pkg_file;
struct pkg_dir;
struct pkg_category;
struct pkg_conflict;
struct pkg_script;
struct pkg_option;

struct pkgdb;
struct pkgdb_it;

struct pkg_jobs;
struct pkg_jobs_entry;

struct pkg_repos;
struct pkg_repos_entry;

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
	/**
	 * A package to be upgraded.
	 */
	PKG_UPGRADE = 1 << 3,
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

typedef enum _pkg_jobs_t {
	PKG_JOBS_INSTALL,
	PKG_JOBS_DEINSTALL
} pkg_jobs_t;

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
 * @return the size of the uncompressed new package (PKG_UPGRADE).
 */
int64_t pkg_new_flatsize(struct pkg *);

/**
 * @return the size of the compressed new package (PKG_UPGRADE).
 */
int64_t pkg_new_pkgsize(struct pkg *);


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
int pkg_set(struct pkg *pkg, pkg_attr attr, const char *value);

/**
 * Read the content of a file into a buffer, then call pkg_set().
 */
int pkg_set_from_file(struct pkg *pkg, pkg_attr attr, const char *file);

int pkg_setautomatic(struct pkg *pkg);
int pkg_isautomatic(struct pkg *pkg);

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
int pkg_addrdep(struct pkg *pkg, const char *name, const char *origin, const
			   char *version);

/**
 * Allocate a new struct pkg_file and add it to the files of pkg.
 * @param sha256 The ascii representation of the sha256 or a NULL pointer.
 * @return An error code.
 */
int pkg_addfile(struct pkg *pkg, const char *path, const char *sha256);

/**
 * Add a path
 * @return An error code.
 */
int pkg_adddir(struct pkg *pkg, const char *path);

/**
 * Add a category
 * @return An error code.
 */
int pkg_addcategory(struct pkg *pkg, const char *name);

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
const char *pkg_dep_origin(struct pkg_dep *dep);
const char *pkg_dep_name(struct pkg_dep *dep);
const char *pkg_dep_version(struct pkg_dep *dep);

/* pkg_file */
const char * pkg_file_path(struct pkg_file *);
const char * pkg_file_sha256(struct pkg_file *);

const char *pkg_dir_path(struct pkg_dir *);

const char *pkg_category_name(struct pkg_category *);

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
 * Open the package database.
 * The db must be free'ed with pkgdb_close().
 * @param dbfile Name of the package database file
 * @return An error code.
 */
int pkgdb_open(struct pkgdb **db, pkgdb_t type, const char *dbfile);

/**
 * Close and free the struct pkgdb.
 */
void pkgdb_close(struct pkgdb *db);

/** 
 * Dump the content of the database in yaml format
 * only to use when mtree will be deprecated
 */

int pkgdb_dump(struct pkgdb *db, char *dest);

/**
 * Whether a package database instance has a particular flag.
 * @return 0 if false, true otherwise
 */
int pkgdb_has_flag(struct pkgdb *db, int flag);

/* The flags used in pkgdb_has_flag() */
#define	PKGDB_FLAG_IN_FLIGHT	(1 << 0)

/**
 * Register a package to the database.
 * @return An error code.
 */
int pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg);

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
		match_t type, unsigned int field);

/**
 * Query the remote database.
 * @param db A pkgdb opened with PKGDB_REMOTE.
 * @warning Returns NULL on failure.
 */
struct pkg * pkgdb_query_remote(struct pkgdb *db, const char *pattern);

/**
 * 
 */
struct pkgdb_it *pkgdb_query_upgrades(struct pkgdb *db);
struct pkgdb_it *pkgdb_query_downgrades(struct pkgdb *db);
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

#define REPO_SEARCH_NAME 0
#define REPO_SEARCH_COMMENT (1<<0)
#define REPO_SEARCH_DESCRIPTION (1<<1)


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
int pkgdb_loaddirs(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadscripts(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadoptions(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadmtree(struct pkgdb *db, struct pkg *pkg);
int pkgdb_loadcategory(struct pkgdb *db, struct pkg *pkg);

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
int pkg_add(struct pkgdb *db, const char *path);

/**
 * Create a new jobs object
 * @return EPKG_OK on success, EPKG_FATAL on error
 */
int pkg_jobs_new(struct pkg_jobs **jobs);

/**
 * Allocate a new pkg_jobs_entry object.
 * @param db A pkgdb open with PKGDB_REMOTE.
 * @return An error code.
 */
int pkg_jobs_new_entry(struct pkg_jobs *jobs, struct pkg_jobs_entry **je, pkg_jobs_t type, struct pkgdb *db);

/**
 * Free a pkg_jobs object
 */
void pkg_jobs_free(struct pkg_jobs *jobs);

/**
 * Add a pkg to the jobs entry queue.
 * @return An error code.
 */
int pkg_jobs_add(struct pkg_jobs_entry *je, struct pkg *pkg);

/**
 * Iterates over the jobs entry objects
 * @param je Returns the next entry in a jobs object.
 * Must be set to NULL for the first call.
 * @return EPKG_OK on success, EPKG_END if end of tail is reached.
 */
int pkg_jobs(struct pkg_jobs *jobs, struct pkg_jobs_entry **je);

/**
 * Iterates over the packages in the jobs queue.
 * @param pkg Must be set to NULL for the first call.
 * @return An error code.
 */
int pkg_jobs_entry(struct pkg_jobs_entry *je, struct pkg **pkg);

/**
 * Apply the jobs in the queue (fetch/install/deinstall).
 * @return An error code.
 */
int pkg_jobs_apply(struct pkg_jobs_entry *je, int force);

/**
 * Checks if a given job is already added in another jobs entry
 * @param jobs A valid jobs object as received from pkg_jobs_new()
 * @param pkg A package that will be checked if it exists already as a job
 * @param res A pkg where to store the result if a package is found to exist
 * @return EPKG_OK if the is not added yet and EPKG_FATAL otherwise
 */
int pkg_jobs_exists(struct pkg_jobs *jobs, struct pkg *pkg, struct pkg **res);

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
int pkg_delete(struct pkg *pkg, struct pkgdb *db, int force);

int pkg_repo_fetch(struct pkg *pkg);

/**
 * Initializes the repositories object
 * @return EPKG_OK on success, otherwise EPKG_FATAL
 */
int pkg_repos_new(struct pkg_repos **repos);

/**
 * Loads the remote repositories from file
 * @return EPKG_OK on success, and EPKG_FATAL on error
 */
int pkg_repos_load(struct pkg_repos *repos);

/**
 * Adds a repository entry to the tail
 * @param repos A valid repository object received from pkg_repos_new()
 * @param re A valid repository entry object
 * @return EPKG_OK on success, EPKG_FATAL on error
 */
int pkg_repos_add(struct pkg_repos *repos, struct pkg_repos_entry *re);

/**
 * Adds a repository entry to a package object
 * @param pkg A valid package object
 * @param re A valid repository entry object
 * @return EPKG_OK on success, EPKG_FATAL on error
 */
int pkg_repos_add_in_pkg(struct pkg *pkg, struct pkg_repos_entry *re);

/**
 * Get the next repository from the tail
 * @param repos A valid repository pointer as returned by pkg_repos_new()
 * @param re A pointer to a repository entry to save the result. Must be set to
 * NULL for the first repository entry
 * @return EPKG_OK on success, EPKG_END if end of repository is reached
 */
int pkg_repos_next(struct pkg_repos *repos, struct pkg_repos_entry **re);

/**
 * Get the next repository assigned to a package object
 * @param pkg A valid package object
 * @param re A pointer to a repository entry to save the result. Must be set to
 * NULL for the first repository entry
 * @return EPKG_OK on success, EPKG_END if end of repository is reached
 */
int pkg_repos_next_in_pkg(struct pkg *pkg, struct pkg_repos_entry **re);

/**
 * Returns the name associated with a repository entry object
 * @param re A valid repository entry object
 */
const char * pkg_repos_get_name(struct pkg_repos_entry *re);

/**
 * Returns the URL associated wth a repository entry object
 * @param re A valid repository entry object
 */
const char * pkg_repos_get_url(struct pkg_repos_entry *re);

/**
 * Returns the line in the configuration where a repository is found
 * @param re A valid repository entry
 */
unsigned int pkg_repos_get_line(struct pkg_repos_entry *re);

/**
 * Free the memory used by the repository objects
 */
void pkg_repos_free(struct pkg_repos *repos);

/**
 * Free the memory used by the repository objects in a package
 */
void pkg_repos_free_in_pkg(struct pkg *pkg);

/**
 * Get the value of a configuration key
 */
const char * pkg_config(const char *key);

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
	PKG_EVENT_FETCHING,
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
	uint16_t line;
	const char *file;
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
			struct pkg_dep *dep;
		} e_missing_dep;
		struct {
			struct pkg *pkg;
			int force;
		} e_required;
		struct {
			struct pkg *pkg;
		} e_failed_cksum;
	};
};

/**
 * Event callback mechanism.  Events will be reported using this callback,
 * providing an event identifier and up to two event-specific pointers.
 */
typedef int(*pkg_event_cb)(void *, struct pkg_event *);

void pkg_event_register(pkg_event_cb cb, void *data);

#endif
