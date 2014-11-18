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

#include <sys/cdefs.h>
#include <sys/sbuf.h>
#include <sys/types.h>

#include <stdbool.h>
#include <uthash.h>
#include <utlist.h>
#include <ucl.h>

#include "private/utils.h"
#include "private/pkg.h"
#include "pkg.h"
#include "tree.h"

struct pkg_jobs;
struct job_pattern;

struct pkg_job_universe_item {
	struct pkg *pkg;
	int priority;
	bool processed;
	UT_hash_handle hh;
	struct pkg_job_universe_item *next, *prev;
};

struct pkg_job_request_item {
	struct pkg *pkg;
	struct pkg_job_universe_item *unit;
	struct job_pattern *jp;
	struct pkg_job_request_item *prev, *next;
};

struct pkg_job_request {
	struct pkg_job_request_item *item;
	bool skip;
	bool automatic;
	UT_hash_handle hh;
};

struct pkg_solved {
	struct pkg_job_universe_item *items[2];
	pkg_solved_t type;
	bool already_deleted;
	struct pkg_solved *prev, *next;
};

struct pkg_job_seen {
	struct pkg_job_universe_item *un;
	const char *digest;
	UT_hash_handle hh;
};

struct pkg_job_provide {
	struct pkg_job_universe_item *un;
	const char *provide;
	struct pkg_job_provide *next, *prev;
	UT_hash_handle hh;
};

struct pkg_job_replace {
	char *new_uid;
	char *old_uid;
	struct pkg_job_replace *next;
};


struct pkg_jobs_universe {
	struct pkg_job_universe_item *items;
	struct pkg_job_seen *seen;
	struct pkg_job_provide *provides;
	struct pkg_job_replace *uid_replaces;
	struct pkg_jobs *j;
	size_t nitems;
};

struct pkg_jobs_conflict_item {
	uint64_t hash;
	struct pkg_job_universe_item *item;
	TREE_ENTRY(pkg_jobs_conflict_item) entry;
};

struct pkg_jobs {
	struct pkg_jobs_universe *universe;
	struct pkg_job_request	*request_add;
	struct pkg_job_request	*request_delete;
	struct pkg_solved *jobs;
	struct pkgdb	*db;
	pkg_jobs_t	 type;
	pkg_flags	 flags;
	int		 solved;
	int count;
	int total;
	int conflicts_registered;
	bool need_fetch;
	const char *reponame;
	const char *destdir;
	TREE_HEAD(, pkg_jobs_conflict_item) *conflict_items;
	struct job_pattern *patterns;
};

struct job_pattern {
	char		*pattern;
	char		*path;
	match_t		match;
	bool		is_file;
	UT_hash_handle hh;
};

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
 * Check if the specified digest was seen in the universe
 */
struct pkg_job_seen* pkg_jobs_universe_seen(struct pkg_jobs_universe *universe,
	const char *digest);

/*
 * Search for an entry corresponding to the uid in the universe
 */
struct pkg_job_universe_item* pkg_jobs_universe_find(struct pkg_jobs_universe
	*universe, const char *uid);

/*
 * Add a single package to the universe
 */
int pkg_jobs_universe_add_pkg(struct pkg_jobs_universe *universe,
	struct pkg *pkg, bool force, struct pkg_job_universe_item **found);

/*
 * Change uid for universe item
 */
void pkg_jobs_universe_change_uid(struct pkg_jobs_universe *universe,
	struct pkg_job_universe_item *unit,
	const char *new_uid, size_t uidlen, bool update_rdeps);

/*
 * Find remote package in db or universe
 */
struct pkg* pkg_jobs_universe_get_remote(struct pkg_jobs_universe *universe,
	const char *uid, unsigned flag);

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
int pkg_conflicts_append_chain(struct pkg_job_universe_item *it,
	struct pkg_jobs *j);
/*
 * Perform integrity check for the jobs specified
 */
int pkg_conflicts_integrity_check(struct pkg_jobs *j);
/*
 * Register a conflict between two packages
 */
void pkg_conflicts_register(struct pkg *p1, struct pkg *p2,
		enum pkg_conflict_type type);

/*
 * Check whether `rp` is an upgrade for `lp`
 */
bool pkg_jobs_need_upgrade(struct pkg *rp, struct pkg *lp);

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
 * - if `forece` is false then *all* candidates are added to the universe, but
 * merely if *any* of remote packages is an upgrade for local one
 */
struct pkg_job_universe_item*
pkg_jobs_universe_get_upgrade_candidates(struct pkg_jobs_universe *universe,
	const char *uid, struct pkg *lp, bool force);

/*
 * Free job request (with all candidates)
 */
void pkg_jobs_request_free(struct pkg_job_request *req);

#endif /* PKG_JOBS_H_ */
