/*-
 * Copyright (c) 2024 The FreeBSD Foundation
 *
 * This software was developed by Isaac Freund <ifreund@freebsdfoundation.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <assert.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkg_jobs.h"

#define dbg(x, ...) pkg_dbg(PKG_DBG_SCHEDULER, x, __VA_ARGS__)

extern struct pkg_ctx ctx;

static const char *
pkg_jobs_schedule_job_type_string(struct pkg_solved *job)
{
	switch (job->type) {
	case PKG_SOLVED_INSTALL:
		return "install";
	case PKG_SOLVED_DELETE:
		return "delete";
	case PKG_SOLVED_UPGRADE:
		return "upgrade";
	case PKG_SOLVED_UPGRADE_INSTALL:
		return "split upgrade install";
	case PKG_SOLVED_UPGRADE_REMOVE:
		return "split upgrade delete";
	default:
		assert(false);
	}
}

/*
 * Returns true if pkg a directly depends on pkg b.
 *
 * Checking only direct dependencies is sufficient to define the edges in a
 * graph that models indirect dependencies as well as long as all of the
 * intermediate dependencies are also nodes in the graph.
 */
static bool pkg_jobs_schedule_direct_depends(struct pkg *a, struct pkg *b)
{
	struct pkg_dep *dep = NULL;
	while (pkg_deps(a, &dep) == EPKG_OK) {
		if (STREQ(b->uid, dep->uid)) {
			return (true);
		}
	}
	return (false);
}

/* Enable debug logging in pkg_jobs_schedule_graph_edge() */
static bool debug_edges = false;

/*
 * Jobs are nodes in a directed graph. Edges represent job scheduling order
 * requirements. The existence of an edge from node A to node B indicates
 * that job A must be executed before job B.
 *
 * There is a directed edge from node A to node B if and only if
 * one of the following conditions holds:
 *
 * 1. B's new package depends on A's new package
 * 2. A's old package depends on B's old package
 * 3. A's old package conflicts with B's new package
 * 4. A and B are the two halves of a split upgrade job
 *    and A is the delete half.
 */
static bool
pkg_jobs_schedule_graph_edge(struct pkg_solved *a, struct pkg_solved *b)
{
	if (a == b) {
		return (false);
	}

	if (a->xlink == b || b->xlink == a) {
		assert(a->xlink == b && b->xlink == a);
		assert(a->type == PKG_SOLVED_UPGRADE_INSTALL ||
		       a->type == PKG_SOLVED_UPGRADE_REMOVE);
		assert(b->type == PKG_SOLVED_UPGRADE_INSTALL ||
		       b->type == PKG_SOLVED_UPGRADE_REMOVE);
		assert(a->type != b->type);

		bool edge = a->type == PKG_SOLVED_UPGRADE_REMOVE;
		if (edge && debug_edges) {
			dbg(4, "  edge to %s %s, split upgrade",
			    pkg_jobs_schedule_job_type_string(b),
			    b->items[0]->pkg->uid);
		}
		return (edge);
	}

	/* TODO: These switches would be unnecessary if delete jobs used
	 * items[1] rather than items[0]. I suspect other cleanups could
	 * be made as well. */
	struct pkg *a_new = NULL;
	struct pkg *a_old = NULL;
	switch (a->type) {
	case PKG_SOLVED_INSTALL:
	case PKG_SOLVED_UPGRADE_INSTALL:
		a_new = a->items[0]->pkg;
		break;
	case PKG_SOLVED_DELETE:
	case PKG_SOLVED_UPGRADE_REMOVE:
		a_old = a->items[0]->pkg;
		break;
	case PKG_SOLVED_UPGRADE:
		a_new = a->items[0]->pkg;
		a_old = a->items[1]->pkg;
		break;
	default:
		assert(false);
	}

	struct pkg *b_new = NULL;
	struct pkg *b_old = NULL;
	switch (b->type) {
	case PKG_SOLVED_INSTALL:
	case PKG_SOLVED_UPGRADE_INSTALL:
		b_new = b->items[0]->pkg;
		break;
	case PKG_SOLVED_DELETE:
	case PKG_SOLVED_UPGRADE_REMOVE:
		b_old = b->items[0]->pkg;
		break;
	case PKG_SOLVED_UPGRADE:
		b_new = b->items[0]->pkg;
		b_old = b->items[1]->pkg;
		break;
	default:
		assert(false);
	}

	if (a_new != NULL && b_new != NULL &&
	    pkg_jobs_schedule_direct_depends(a_new, b_new)) {
		if (debug_edges) {
			dbg(4, "  edge to %s %s, new depends on new",
			    pkg_jobs_schedule_job_type_string(b),
			    b->items[0]->pkg->uid);
		}
		return (true);
	} else if (a_old != NULL && b_old != NULL &&
		   pkg_jobs_schedule_direct_depends(a_old, b_old)) {
		if (debug_edges) {
			dbg(4, "  edge to %s %s, old depends on old",
			    pkg_jobs_schedule_job_type_string(b),
			    b->items[0]->pkg->uid);
		}
		return (true);
	} else if (a_old != NULL && b_new != NULL) {
		struct pkg_conflict *conflict = NULL;
		while (pkg_conflicts(a_old, &conflict) == EPKG_OK) {
			if (STREQ(b_new->uid, conflict->uid)) {
				if (debug_edges) {
					dbg(4, "  edge to %s %s, old conflicts with new",
					    pkg_jobs_schedule_job_type_string(b),
					    b->items[0]->pkg->uid);
				}
				return (true);
			}
		}
	}

	return (false);
}

static void
pkg_jobs_schedule_dbg_job(pkg_solved_list *jobs, struct pkg_solved *job)
{
	if (ctx.debug_level < 4) {
		return;
	}

	dbg(4, "job: %s %s", pkg_jobs_schedule_job_type_string(job),
	    job->items[0]->pkg->uid);

	debug_edges = true;
	vec_foreach(*jobs, i) {
		if (jobs->d[i] == NULL)
			continue;
		pkg_jobs_schedule_graph_edge(job, jobs->d[i]);
	}
	debug_edges = false;
}

static bool
pkg_jobs_schedule_has_incoming_edge(pkg_solved_list *nodes,
    struct pkg_solved *node)
{
	vec_foreach(*nodes, i) {
		if (nodes->d[i] == NULL)
			continue;
		if (pkg_jobs_schedule_graph_edge(nodes->d[i], node)) {
			return (true);
		}
	}
	return (false);
}

/*
 * Prioritizing the install jobs and deprioritizing the delete jobs of split
 * upgrades reduces the distance between the two halves of the split job in the
 * final execution order.
 */
static int
pkg_jobs_schedule_priority(struct pkg_solved *node)
{
	switch (node->type) {
	case PKG_SOLVED_UPGRADE_INSTALL:
		return 1;
	case PKG_SOLVED_UPGRADE_REMOVE:
		return -1;
	default:
		return 0;
	}
}

/* This comparison function is used as a tiebreaker in the topological sort. */
static int
pkg_jobs_schedule_cmp_available(const void *va, const void *vb)
{
	struct pkg_solved *a = *(struct pkg_solved **)va;
	struct pkg_solved *b = *(struct pkg_solved **)vb;

	int ret = pkg_jobs_schedule_priority(b) - pkg_jobs_schedule_priority(a);
	if (ret == 0) {
		/* Falling back to lexicographical ordering ensures that job execution
		 * order is always consistent and makes testing easier. */
		return strcmp(b->items[0]->pkg->uid, a->items[0]->pkg->uid);
	} else {
		return ret;
	}
}

/* Topological sort based on Kahn's algorithm with a tiebreaker */
static void
pkg_jobs_schedule_topological_sort(pkg_solved_list *jobs)
{
	pkg_solved_list sorted = vec_init();
	pkg_solved_list available = vec_init();
	size_t left = jobs->len;

	/* Place all job nodes with no incoming edges in available */
	vec_foreach(*jobs, i) {
		if (!pkg_jobs_schedule_has_incoming_edge(jobs, jobs->d[i]) &&
		    !pkg_jobs_schedule_has_incoming_edge(&available, jobs->d[i])) {
			vec_push(&available, jobs->d[i]);
			jobs->d[i] = NULL;
			left--;
		}
	}

	while (available.len > 0) {
		/* Add the highest priority job from the set of available jobs
		 * to the sorted list */
		qsort(available.d, available.len, sizeof(available.d[0]), pkg_jobs_schedule_cmp_available);
		struct pkg_solved *node = vec_pop(&available);
		vec_push(&sorted, node);

		/* Again, place all job nodes with no incoming edges in the set
		 * of available jobs, ignoring any incoming edges from job nodes
		 * already added to the sorted list */
		vec_foreach(*jobs, i) {
			if (jobs->d[i] == NULL)
				continue;
			if (pkg_jobs_schedule_graph_edge(node, jobs->d[i])) {
				if (!pkg_jobs_schedule_has_incoming_edge(jobs, jobs->d[i]) &&
				    !pkg_jobs_schedule_has_incoming_edge(&available, jobs->d[i])) {
					vec_push(&available, jobs->d[i]);
					jobs->d[i] = NULL;
					left--;
				}
			}
		}
	}

	/* The jobs list will only be non-empty at this point if there is a
	 * cycle in the graph and all cycles must be eliminated by splitting
	 * upgrade jobs before calling this function. */
	assert(left == 0);

	vec_free(&available);
	free(jobs->d);
	jobs->d = sorted.d;
}

/*
 * This is a depth-first search that keeps track of the path taken to the
 * current node in the graph. If a node on this path is encountered a
 * second time a cycle has been found.
 */
static struct pkg_solved *
pkg_jobs_schedule_find_cycle(pkg_solved_list *jobs,
    struct pkg_solved **path, struct pkg_solved *node)
{
	/* Push node to path */
	assert(node->mark == PKG_SOLVED_CYCLE_MARK_NONE);
	node->mark = PKG_SOLVED_CYCLE_MARK_PATH;
	assert(node->path_next == NULL);
	node->path_next = *path;
	*path = node;

	vec_foreach(*jobs, i) {
		if (pkg_jobs_schedule_graph_edge(node, jobs->d[i])) {
			switch (jobs->d[i]->mark){
			case PKG_SOLVED_CYCLE_MARK_NONE:;
				struct pkg_solved *cycle =
				    pkg_jobs_schedule_find_cycle(jobs, path, jobs->d[i]);
				if (cycle != NULL) {
					return (cycle);
				}
				break;
			case PKG_SOLVED_CYCLE_MARK_DONE:
				break;
			case PKG_SOLVED_CYCLE_MARK_PATH:
				return (jobs->d[i]); /* Found a cycle */
			default:
				assert(false);
			}
		}
	}

	/* Pop node from path */
	assert(node->mark == PKG_SOLVED_CYCLE_MARK_PATH);
	node->mark = PKG_SOLVED_CYCLE_MARK_DONE;
	*path = node->path_next;
	node->path_next = NULL;

	return (NULL);
}

int pkg_jobs_schedule(struct pkg_jobs *j)
{
	while (true) {
		dbg(3, "checking job scheduling graph for cycles...");

		vec_foreach(j->jobs, i) {
			j->jobs.d[i]->mark = PKG_SOLVED_CYCLE_MARK_NONE;
			j->jobs.d[i]->path_next = NULL;

			pkg_jobs_schedule_dbg_job(&j->jobs, j->jobs.d[i]);
		}

		/* The graph may not be connected, in which case it is necessary to
		 * run multiple searches for cycles from different start nodes. */
		struct pkg_solved *path = NULL;
		struct pkg_solved *cycle = NULL;
		vec_foreach(j->jobs, i) {
			switch (j->jobs.d[i]->mark) {
			case PKG_SOLVED_CYCLE_MARK_NONE:
				cycle = pkg_jobs_schedule_find_cycle(&j->jobs, &path, j->jobs.d[i]);
				break;
			case PKG_SOLVED_CYCLE_MARK_DONE:
				break;
			case PKG_SOLVED_CYCLE_MARK_PATH:
			default:
				assert(false);
			}
			if (cycle != NULL) {
				break;
			}
		}

		if (cycle == NULL) {
			dbg(3, "no job scheduling graph cycles found");
			assert(path == NULL);
			break;
		}

		dbg(3, "job scheduling graph cycle found");
		assert(path != NULL);
		assert(path != cycle);

		/* Choose an arbitrary upgrade job in the cycle to split in order
		 * to break the cycle.
		 *
		 * TODO: Does it truly not matter which upgrade job in the cycle we
		 * choose to split? I'm relatively confident that splitting any upgrade job
		 * will break the given cycle but is it possible that one of the choices
		 * would break additional cycles as well?
		 */
		while (path->type != PKG_SOLVED_UPGRADE && path->items[1]->pkg->conflicts == NULL) {
			if (path == cycle) {
				pkg_emit_error("found job scheduling cycle without upgrade job");
			 	return (EPKG_FATAL);
			}
			path = path->path_next;
			assert(path != NULL);
		}

		/* path is now the upgrade job chosen to be split */
		dbg(2, "splitting upgrade %s job", path->items[0]->pkg->uid);

		struct pkg_solved *new = xcalloc(1, sizeof(struct pkg_solved));
		new->type = PKG_SOLVED_UPGRADE_REMOVE;
		new->items[0] = path->items[1];
		new->xlink = path;
		path->type = PKG_SOLVED_UPGRADE_INSTALL;
		path->items[1] = NULL;
		path->xlink = new;
		vec_push(&j->jobs, new);
	}

	pkg_jobs_schedule_topological_sort(&j->jobs);

	dbg(3, "finished job scheduling");

	return (EPKG_OK);
}
