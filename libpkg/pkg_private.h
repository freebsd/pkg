#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/types.h>
#include <stdbool.h>

#include <archive.h>

#include "pkg_util.h"

#define PKG_NUM_FIELDS 15

#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM| \
		ARCHIVE_EXTRACT_TIME  |ARCHIVE_EXTRACT_ACL | \
		ARCHIVE_EXTRACT_FFLAGS|ARCHIVE_EXTRACT_XATTR)

struct pkg {
	struct {
		struct sbuf *value;
		int type; /* for which pkg_t this field is defined */
		unsigned int optional :1;
	} fields[PKG_NUM_FIELDS];
	bool automatic;
	int64_t flatsize;
	int64_t new_flatsize;
	int64_t new_pkgsize;
	STAILQ_HEAD(categories, pkg_category) categories;
	STAILQ_HEAD(deps, pkg_dep) deps;
	STAILQ_HEAD(rdeps, pkg_dep) rdeps;
	STAILQ_HEAD(files, pkg_file) files;
	STAILQ_HEAD(dirs, pkg_dir) dirs;
	STAILQ_HEAD(conflicts, pkg_conflict) conflicts;
	STAILQ_HEAD(scripts, pkg_script) scripts;
	STAILQ_HEAD(options, pkg_option) options;
	STAILQ_HEAD(repos_entry, pkg_repos_entry) repos;
	int flags;
	int64_t rowid;
	pkg_t type;
	STAILQ_ENTRY(pkg) next;
};

struct pkg_dep {
	struct sbuf *origin;
	struct sbuf *name;
	struct sbuf *version;
	STAILQ_ENTRY(pkg_dep) next;
};

struct pkg_category {
	char name[BUFSIZ];
	STAILQ_ENTRY(pkg_category) next;
};

struct pkg_file {
	char path[MAXPATHLEN];
	char sha256[65];
	STAILQ_ENTRY(pkg_file) next;
};

struct pkg_dir {
	char path[MAXPATHLEN];
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
	struct pkg_jobs_entry {
		STAILQ_HEAD(jobs, pkg) jobs;
		LIST_HEAD(nodes, pkg_jobs_node) nodes;
		struct pkgdb *db;
		pkg_jobs_t type;
		unsigned int resolved :1;
		STAILQ_ENTRY(pkg_jobs_entry) next;
	} j;

	STAILQ_HEAD(jobs_multi, pkg_jobs_entry) multi;
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
		char *name;
		char *url;
		unsigned int line;
		STAILQ_ENTRY(pkg_repos_entry) entries;
	} r;

	STAILQ_HEAD(repos, pkg_repos_entry) nodes;
};

int pkg_open2(struct pkg **p, struct archive **a, struct archive_entry **ae, const char *path);
void pkg_freecategories(struct pkg *pkg);
void pkg_freedeps(struct pkg *pkg);
void pkg_freerdeps(struct pkg *pkg);
void pkg_freefiles(struct pkg *pkg);
void pkg_freedirs(struct pkg *pkg);
void pkg_freeconflicts(struct pkg *pkg);
void pkg_freescripts(struct pkg *pkg);
void pkg_freeoptions(struct pkg *pkg);

int pkg_dep_new(struct pkg_dep **);
void pkg_dep_free(struct pkg_dep *);

int pkg_file_new(struct pkg_file **);
void pkg_file_free(struct pkg_file *);

int pkg_dir_new(struct pkg_dir **);
void pkg_dir_free(struct pkg_dir *);

int pkg_category_new(struct pkg_category **);
void pkg_category_free(struct pkg_category *);

int pkg_conflict_new(struct pkg_conflict **);
void pkg_conflict_free(struct pkg_conflict *);

int pkg_script_new(struct pkg_script **);
void pkg_script_free(struct pkg_script *);

int pkg_option_new(struct pkg_option **);
void pkg_option_free(struct pkg_option *);

int pkg_jobs_resolv(struct pkg_jobs_entry *je);
void pkg_jobs_free_entry(struct pkg_jobs_entry *je);

struct packing;

int packing_init(struct packing **pack, const char *path, pkg_formats format);
int packing_append_file(struct packing *pack, const char *filepath, const char *newpath);
int packing_append_buffer(struct packing *pack, const char *buffer, const char *path, int size);
int packing_append_tree(struct packing *pack, const char *treepath, const char *newroot);
int packing_finish(struct packing *pack);
pkg_formats packing_format_from_string(const char *str);

int pkg_delete_files(struct pkg *pkg, int force);
int pkg_delete_dirs(struct pkg *pkg, int force);

#endif
