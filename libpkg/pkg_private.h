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

#include "pkg_util.h"

#define PKG_NUM_FIELDS 17

#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM| \
		ARCHIVE_EXTRACT_TIME  |ARCHIVE_EXTRACT_ACL | \
		ARCHIVE_EXTRACT_FFLAGS|ARCHIVE_EXTRACT_XATTR)

struct pkg {
	struct sbuf * fields[PKG_NUM_FIELDS];
	bool automatic;
	int64_t flatsize;
	int64_t new_flatsize;
	int64_t new_pkgsize;
	STAILQ_HEAD(categories, pkg_category) categories;
	STAILQ_HEAD(licenses, pkg_license) licenses;
	STAILQ_HEAD(deps, pkg_dep) deps;
	STAILQ_HEAD(rdeps, pkg_dep) rdeps;
	STAILQ_HEAD(files, pkg_file) files;
	STAILQ_HEAD(dirs, pkg_dir) dirs;
	STAILQ_HEAD(conflicts, pkg_conflict) conflicts;
	STAILQ_HEAD(scripts, pkg_script) scripts;
	STAILQ_HEAD(options, pkg_option) options;
	STAILQ_HEAD(users, pkg_user) users;
	STAILQ_HEAD(groups, pkg_group) groups;
	int flags;
	int64_t rowid;
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
	char sha256[SHA256_DIGEST_LENGTH * 2 +1];
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
	int try;
	STAILQ_ENTRY(pkg_dir) next;
};

struct pkg_conflict {
	struct sbuf *glob;
	STAILQ_ENTRY(pkg_conflict) next;
};

struct pkg_script {
	struct sbuf *data;
	pkg_script_t type;
	STAILQ_ENTRY(pkg_script) next;
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

struct pkg_repos {
	struct pkg_repos_entry {
		struct sbuf *name;
		struct sbuf *url;
		unsigned int line;
		unsigned int switched :1;
		STAILQ_ENTRY(pkg_repos_entry) entries;
	} re;

	unsigned int switchable :1;
	STAILQ_HEAD(repos, pkg_repos_entry) nodes;
};

struct pkg_user {
	char name[MAXLOGNAME+1];
	char uidstr[8192]; /* taken from pw_util.c */
	STAILQ_ENTRY(pkg_user) next;
};

struct pkg_group {
	char name[MAXLOGNAME+1];
	char gidstr[8192]; /* taken from gw_util.c */
	STAILQ_ENTRY(pkg_group) next;
};

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

int pkg_conflict_new(struct pkg_conflict **);
void pkg_conflict_free(struct pkg_conflict *);

int pkg_script_new(struct pkg_script **);
void pkg_script_free(struct pkg_script *);

int pkg_option_new(struct pkg_option **);
void pkg_option_free(struct pkg_option *);

int pkg_user_new(struct pkg_user **);
void pkg_user_free(struct pkg_user *);

int pkg_group_new(struct pkg_group **);
void pkg_group_free(struct pkg_group *);

int pkg_jobs_resolv(struct pkg_jobs *jobs);

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

int pkg_set_rowid(struct pkg *, int64_t rowid);

/* pkgdb commands */
int sql_exec(sqlite3 *, const char *, ...);

#endif
