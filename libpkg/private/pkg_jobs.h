/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef PKG_JOBS_H_
#define PKG_JOBS_H_

#include <sys/types.h>

#include <stdbool.h>
#include <ucl.h>

#include "private/utils.h"
#include "private/pkg.h"
#include "pkg.h"

struct pkg_jobs;
struct job_pattern;

/*
 * Each item in pkg_jobs_universe->items is part of a universe_itemv_t vec
 * keyed by the package uid. All items sharing the same uid are stored
 * together in the same vec.
 */
struct pkg_job_universe_item {
	struct pkg *pkg;
	bool processed;
	bool cudf_emit_skip;
};
typedef vec_t(struct pkg_job_universe_item *) universe_itemv_t;

struct pkg_job_request_item {
	struct pkg *pkg;
	struct pkg_job_universe_item *unit;
	struct job_pattern *jp;
};
typedef vec_t(struct pkg_job_request_item) request_itemv_t;

struct pkg_job_request {
	request_itemv_t items;
	bool skip;
	bool processed;
	bool automatic;
};

enum pkg_solved_cycle_mark {
	PKG_SOLVED_CYCLE_MARK_NONE,	/* Not yet checked */
	PKG_SOLVED_CYCLE_MARK_DONE,	/* Finished checking */
	PKG_SOLVED_CYCLE_MARK_PATH,	/* In the path currently being checked */
};

/*
 * The usage of the items field depends on the value of the type field:
 *
 * PKG_SOLVED_FETCH,
 * PKG_SOLVED_INSTALL,
 * PKG_SOLVED_UPGRADE_INSTALL:
 *   items[0] is the new package to be installed/fetched
 *   items[1] is NULL
 *
 * PKG_SOLVED_DELETE,
 * PKG_SOLVED_UPGRADE_REMOVE:
 *   items[0] is the old package to be deleted
 *   items[1] is NULL
 *
 * PKG_SOLVED_UPGRADE:
 *   items[0] is the new package to be installed
 *   items[1] is the old package to be deleted
 */
struct pkg_solved {
	struct pkg_job_universe_item *items[2];
	struct pkg_solved *xlink;	/* link split jobs together */
	pkg_solved_t type;
	enum pkg_solved_cycle_mark mark;/* scheduling cycle detection */
	struct pkg_solved *path_prev;	/* scheduling cycle detection */
};
typedef vec_t(struct pkg_solved *) pkg_solved_list;

struct pkg_job_provide {
	struct pkg_job_universe_item *un;
	const char *provide;
	bool is_shlib;
};
typedef vec_t(struct pkg_job_provide) providev_t;

struct pkg_jobs_universe {
	pkghash *items;		/* package uid -> universe_itemv_t * */
	pkghash *seen;		/* package digest -> universe_itemv_t * */
	pkghash *provides;	/* shlibs, pkg_job_provide */
	struct pkg_jobs *j;
	size_t nitems;
	int rdeps_depth;	/* track rdeps recursion to prevent explosion */
};

struct pkg_jobs_conflict_item {
	uint64_t hash;
	struct pkg_job_universe_item *item;
};
typedef vec_t(struct pkg_jobs_conflict_item) conflict_itemv_t;

struct job_pattern {
	char		*pattern;
	char		*path;
	match_t		 match;
	int		 flags;
};
typedef vec_t(struct job_pattern) job_patternv_t;

struct pkg_jobs {
	struct pkg_jobs_universe *universe;
	pkghash	*request_add;
	pkghash	*request_delete;
	pkg_solved_list	 jobs;
	struct pkgdb	*db;
	pkg_jobs_t	 type;
	pkg_flags	 flags;
	bool solved;
	int total;
	int conflicts_registered;
	bool need_fetch;
	c_charv_t *reponames;
	const char *destdir;
	conflict_itemv_t conflict_items;
	job_patternv_t patterns;
	bool conservative;
	bool pinning;
	bool ignore_compat32;
	void		*lockedpkgs;
	struct triggers triggers;
	struct deferred_rc rc;
	struct pkghash *orphaned;
	struct pkghash *notorphaned;
	charv_t system_shlibs;
};

#define PKG_PATTERN_FLAG_FILE (1 << 0)
#define PKG_PATTERN_FLAG_VULN (1 << 1)

enum pkg_priority_update_type {
	PKG_PRIORITY_UPDATE_REQUEST = 0,
	PKG_PRIORITY_UPDATE_UNIVERSE,
	PKG_PRIORITY_UPDATE_CONFLICT,
	PKG_PRIORITY_UPDATE_DELETE
};

/*
 * Update priorities for all items related with the specified item
 */
void pkg_jobs_update_universe_priority(struct pkg_jobs_universe *universe,
	struct pkg_job_universe_item *it, enum pkg_priority_update_type type);
/*
 * Update priority as the conflict was found
 */
void pkg_jobs_update_conflict_priority(struct pkg_jobs_universe *universe,
	struct pkg_solved *req);

/*
 * Free universe
 */
void pkg_jobs_universe_free(struct pkg_jobs_universe *universe);

/*
 * Create universe for jobs
 */
struct pkg_jobs_universe * pkg_jobs_universe_new(struct pkg_jobs *j);

/*
 * Add a package to the universe
 */
int pkg_jobs_universe_process(struct pkg_jobs_universe *universe,
	struct pkg *pkg);

/*
 * Add a package to the universe and store resulting item in `result`
 */
int pkg_jobs_universe_process_item(struct pkg_jobs_universe *universe,
	struct pkg *pkg, struct pkg_job_universe_item **result);

/*
 * Search for an entry corresponding to the uid in the universe
 */
universe_itemv_t* pkg_jobs_universe_find(struct pkg_jobs_universe
	*universe, const char *uid);

/*
 * Add a single package to the universe
 */
int pkg_jobs_universe_add_pkg(struct pkg_jobs_universe *universe,
	struct pkg *pkg, struct pkg_job_universe_item **found);

/*
 * Find local package in db or universe
 */
struct pkg* pkg_jobs_universe_get_local(struct pkg_jobs_universe *universe,
	const char *uid, unsigned flag);

/*
 * Resolve conflicts in request
 */
int pkg_conflicts_request_resolve(struct pkg_jobs *j);

/*
 * Append conflicts to a package
 */
int pkg_conflicts_append_chain(universe_itemv_t *uv,
	struct pkg_jobs *j);
void pkg_conflicts_free(struct pkg_jobs *j);

/*
 * Check whether `rp` is an upgrade for `lp`
 */
bool pkg_jobs_need_upgrade(charv_t *system_shlibs, struct pkg *rp, struct pkg *lp);

/*
 * Pre-process universe to fix complex upgrade chains
 */
void pkg_jobs_universe_process_upgrade_chains(struct pkg_jobs *j);

/*
 * Find upgrade candidates for a specified local package `lp`
 * This function updates universe as following:
 * - if `lp` is not null it is always added to the universe
 * - if `uid` is in the universe, then the existing upgrade chain is returned
 * - if `force` is true then all candidates are added to the universe
 * - if `force` is false then *all* candidates are added to the universe, but
 * merely if *any* of remote packages is an upgrade for local one
 * - if `version` is not null then ensure we are only adding to the universe
 * packages that match the given version
 */
universe_itemv_t*
pkg_jobs_universe_get_upgrade_candidates(struct pkg_jobs_universe *universe,
	const char *uid, struct pkg *lp, bool force, const char *version);

/*
 * Among a set of job candidates, select the most matching one, depending on job
 * type, repos priorities and other stuff
 */
struct pkg_job_universe_item *
pkg_jobs_universe_select_candidate(universe_itemv_t *chain,
	struct pkg_job_universe_item *local, bool conservative,
	const char *reponame, bool pinning);

/*
 * Determine execution order and sort the pkg_jobs->jobs list.
 */
int pkg_jobs_schedule(struct pkg_jobs *j);

/*
 * Free job request (with all candidates)
 */
void pkg_jobs_request_free(struct pkg_job_request *req);

#endif /* PKG_JOBS_H_ */
