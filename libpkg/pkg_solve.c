/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
 * Copyright (c) 2013-2017 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2024 Serenity Cyber Security, LLC <license@futurecrew.ru>
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
 * All rights reserved.
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

#include <sys/param.h>
#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "pkg.h"
#include "pkghash.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"
#include "picosat.h"

struct pkg_solve_item;

#define dbg(x, ...) pkg_dbg(PKG_DBG_SOLVER, x, __VA_ARGS__)

enum pkg_solve_rule_type {
	PKG_RULE_DEPEND = 0,
	PKG_RULE_UPGRADE_CONFLICT,
	PKG_RULE_EXPLICIT_CONFLICT,
	PKG_RULE_REQUEST_CONFLICT,
	PKG_RULE_REQUEST,
	PKG_RULE_REQUIRE,
	PKG_RULE_VITAL,
	PKG_RULE_MAX
};

static const char *rule_reasons[] = {
	[PKG_RULE_DEPEND] = "dependency",
	[PKG_RULE_UPGRADE_CONFLICT] = "upgrade",
	[PKG_RULE_REQUEST_CONFLICT] = "candidates",
	[PKG_RULE_EXPLICIT_CONFLICT] = "conflict",
	[PKG_RULE_REQUEST] = "request",
	[PKG_RULE_REQUIRE] = "require",
	[PKG_RULE_VITAL] = "vital",
	[PKG_RULE_MAX] = NULL
};

enum pkg_solve_variable_flags {
	PKG_VAR_INSTALL = (1 << 0),
	PKG_VAR_TOP = (1 << 1),
	PKG_VAR_FAILED = (1 << 2),
	PKG_VAR_ASSUMED = (1 << 3),
	PKG_VAR_ASSUMED_TRUE = (1 << 4)
};
struct pkg_solve_variable {
	struct pkg_job_universe_item *unit;
	unsigned int flags;
	int order;
	const char *digest;
	const char *uid;
	const char *assumed_reponame;
};

typedef struct {
    struct pkg_solve_variable *begin;
    size_t count;
} solve_var_slice_t;

struct pkg_solve_item {
	struct pkg_solve_variable *var;
	int inverse;
};

struct pkg_solve_rule {
	enum pkg_solve_rule_type reason;
	vec_t(struct pkg_solve_item) items;
};

struct pkg_solve_problem {
	struct pkg_jobs *j;
	vec_t(struct pkg_solve_rule *) rules;
	pkghash *variables_by_uid;
	struct pkg_solve_variable *variables;
	PicoSAT *sat;
	size_t nvars;
};

/*
 * Utilities to convert jobs to SAT rule
 */

static void
pkg_solve_item_new(struct pkg_solve_rule *rule, struct pkg_solve_variable *var,
    int inverse)
{
	vec_push(&rule->items, ((struct pkg_solve_item){ .var = var, .inverse = inverse }));
}

static struct pkg_solve_rule *
pkg_solve_rule_new(enum pkg_solve_rule_type reason)
{
	struct pkg_solve_rule *result;

	result = xcalloc(1, sizeof(struct pkg_solve_rule));
	result->reason = reason;

	return (result);
}

static void
pkg_solve_variable_set(struct pkg_solve_variable *var,
	struct pkg_job_universe_item *item)
{
	var->unit = item;
	/* XXX: Is it safe to save a ptr here ? */
	var->digest = item->pkg->digest;
	var->uid = item->pkg->uid;
}

static void
pkg_solve_rule_free(struct pkg_solve_rule *rule)
{
	vec_free(&rule->items);
	free(rule);
}

void
pkg_solve_problem_free(struct pkg_solve_problem *problem)
{
	vec_free_and_free(&problem->rules, pkg_solve_rule_free);
	{
		pkghash_foreach(problem->variables_by_uid, it)
			free(it.value);
	}
	pkghash_destroy(problem->variables_by_uid);
	picosat_reset(problem->sat);
	free(problem->variables);
	free(problem);
}

static void
pkg_print_rule_buf(struct pkg_solve_rule *rule, xstring *sb)
{
	struct pkg_solve_item *it, *key_elt = NULL;

	xstring_printf(sb, "%s rule: ", rule_reasons[rule->reason]);
	switch(rule->reason) {
	case PKG_RULE_DEPEND:
		vec_foreach(rule->items, _ri) {
			it = &rule->items.d[_ri];
			if (it->inverse == -1) {
				key_elt = it;
				break;
			}
		}
		if (key_elt) {
			xstring_printf(sb, "package %s%s depends on: ", key_elt->var->uid,
				(key_elt->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
		}
		vec_foreach(rule->items, _rj) {
			it = &rule->items.d[_rj];
			if (it != key_elt) {
				xstring_printf(sb, "%s%s", it->var->uid,
					(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
			}
		}
		break;
	case PKG_RULE_UPGRADE_CONFLICT:
		xstring_printf(sb, "upgrade local %s-%s to remote %s-%s",
			rule->items.d[0].var->uid, rule->items.d[0].var->unit->pkg->version,
			rule->items.d[1].var->uid, rule->items.d[1].var->unit->pkg->version);
		break;
	case PKG_RULE_EXPLICIT_CONFLICT:
		xstring_printf(sb, "The following packages conflict with each other: ");
		vec_foreach(rule->items, _ri) {
			it = &rule->items.d[_ri];
			xstring_printf(sb, "%s-%s%s%s", it->var->unit->pkg->uid, it->var->unit->pkg->version,
				(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)",
				_ri + 1 < rule->items.len ? ", " : "");
		}
		break;
	case PKG_RULE_REQUIRE:
		vec_foreach(rule->items, _ri) {
			it = &rule->items.d[_ri];
			if (it->inverse == -1) {
				key_elt = it;
				break;
			}
		}
		if (key_elt) {
			xstring_printf(sb, "package %s%s depends on a requirement provided by: ",
				key_elt->var->uid,
				(key_elt->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
		}
		vec_foreach(rule->items, _rj) {
			it = &rule->items.d[_rj];
			if (it != key_elt) {
				xstring_printf(sb, "%s%s", it->var->uid,
					(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
			}
		}
		break;
	case PKG_RULE_REQUEST_CONFLICT:
		xstring_printf(sb, "The following packages in request are candidates for installation: ");
		vec_foreach(rule->items, _ri) {
			it = &rule->items.d[_ri];
			xstring_printf(sb, "%s-%s%s", it->var->uid, it->var->unit->pkg->version,
					_ri + 1 < rule->items.len ? ", " : "");
		}
		break;
	case PKG_RULE_VITAL:
		xstring_printf(sb, "The following packages are vital: ");
		vec_foreach(rule->items, _ri) {
			it = &rule->items.d[_ri];
			xstring_printf(sb, "%s-%s%s", it->var->uid, it->var->unit->pkg->version,
					_ri + 1 < rule->items.len ? ", " : "");
		}
		break;
	default:
		break;
	}
}

static void
pkg_debug_print_rule(struct pkg_solve_rule *rule)
{
	xstring *sb;

	if (ctx.debug_level < 3)
		return;

	sb = xstring_new();

	pkg_print_rule_buf(rule, sb);

	xstring_flush(sb);
	dbg(2, "rule: %s", sb->buf);
	xstring_free(sb);
}

static void
pkg_solve_handle_provide(struct pkg_solve_problem *problem,
    struct pkg_job_provide *pr, struct pkg_solve_rule *rule, struct pkg *orig,
    const char *reponame, int *cnt)
{
	struct pkg_solve_variable *curvar;
	struct pkg *pkg;

	/* Find the corresponding variables chain */

	solve_var_slice_t *slice = pkghash_get_value(problem->variables_by_uid, pr->un->pkg->uid);
	if (slice == NULL)
		return;
	for (size_t vi = 0; vi < slice->count; vi++) {
		curvar = &slice->begin[vi];
		/*
		 * For each provide we need to check whether this package
		 * actually provides this require
		 */
		pkg = curvar->unit->pkg;
		if (pr->is_shlib) {
			struct pkg_abi oabi, pabi;

			/*
			 * If this remote package doesn't provide the shared
			 * library in question, then skip it... unless
			 * BACKUP_LIBRARIES is configured, in which case we know
			 * that upgrading to this remote package will not remove
			 * the installed package's shared libraries.
			 */
			if (charv_search(&pkg->shlibs_provided, pr->provide) ==
			    NULL) {
				bool found_local = false;

				if (!ctx.backup_libraries ||
				    pkg->type == PKG_INSTALLED ||
				    (problem->j->type != PKG_JOBS_UPGRADE &&
				     problem->j->type != PKG_JOBS_INSTALL))
					continue;
				for (size_t vj = 0; vj < slice->count; vj++) {
					struct pkg_solve_variable *tmp = &slice->begin[vj];
					struct pkg *tpkg;

					/*
					 * Does the installed version provide
					 * the library in question?  If so, we
					 * can safely upgrade away from it in
					 * the backup_libraries case.
					 */
					tpkg = tmp->unit->pkg;
					if (tpkg->type == PKG_INSTALLED &&
					    charv_search(&tpkg->shlibs_provided,
					    pr->provide) != NULL) {
						found_local = true;
						break;
					}
				}
				if (!found_local)
					continue;
			}

			/*
			 * Make sure the package ABI matches.  If the OS or
			 * architecture don't match, we're done.  Otherwise, on
			 * FreeBSD, the provider's ABI version should be at
			 * least that of the required ABI.  For example, it
			 * should be ok for FreeBSD:15:amd64 packages to depend
			 * on libc.so.7 from a FreeBSD:16:amd64 pkgbase package.
			 * In general we assume that shared libraries provide
			 * backward compatibility.
			 *
			 * This might be reasonable behaviour on other OSes as
			 * well.
			 */
			dbg(2, "origin %s: %s", orig->name, orig->abi);
			dbg(2, "tgt %s: %s", pkg->name, pkg->abi);
			if (!pkg_abi_from_string(&oabi, orig->abi) ||
			    !pkg_abi_from_string(&pabi, pkg->abi))
				continue;
			if (oabi.os != pabi.os ||
			    (oabi.arch != pabi.arch && oabi.arch != PKG_ARCH_ANY && pabi.arch != PKG_ARCH_ANY)  ||
			    (oabi.os != PKG_OS_FREEBSD && oabi.os != PKG_OS_ANY)) {
				dbg(2,
		"require %s: package %s-%s(%c) provides ABI %s, want %s",
				    pr->provide, pkg->name, pkg->version,
				    pkg->type == PKG_INSTALLED ? 'l' : 'r',
				    pkg->abi, orig->abi);
				continue;
			}
			if (pabi.major < oabi.major ||
			    (pabi.major == oabi.major &&
			     pabi.minor < oabi.minor) ||
			    (pabi.major == oabi.major &&
			     pabi.minor == oabi.minor &&
			     pabi.patch < oabi.patch)) {
				continue;
			}
		} else if (charv_search(&pkg->provides, pr->provide) == NULL) {
			dbg(4, "%s provide is not satisfied by %s-%s(%c)",
			    pr->provide, pkg->name, pkg->version,
			    pkg->type == PKG_INSTALLED ? 'l' : 'r');
			continue;
		}

		if (curvar->assumed_reponame == NULL)
			curvar->assumed_reponame = reponame;

		dbg(4, "%s provide is satisfied by %s-%s(%c)",
		    pr->provide, pkg->name, pkg->version,
		    pkg->type == PKG_INSTALLED ? 'l' : 'r');

		pkg_solve_item_new(rule, curvar, 1);
		(*cnt)++;
	}
}

static void
pkg_solve_add_depend_rule(struct pkg_solve_problem *problem,
    struct pkg_solve_variable *var, struct pkg_dep *dep, const char *reponame)
{
	const char *uid;
	struct pkg_solve_variable *curvar;
	struct pkg_solve_rule *rule = NULL;
	int cnt = 0;
	struct pkg_dep *cur;

	/* Dependency rule: (!A | B1 | B2 | B3...) must be true */
	rule = pkg_solve_rule_new(PKG_RULE_DEPEND);
	/* !A */
	pkg_solve_item_new(rule, var, -1);

	/* Process the primary dep and its alternatives */
	for (size_t _dalt = 0; ; _dalt++) {
		struct pkg_dep *cur;
		if (_dalt == 0) {
			cur = dep;
		} else {
			if (_dalt - 1 >= dep->alternatives.len)
				break;
			cur = &dep->alternatives.d[_dalt - 1];
		}
		uid = cur->uid;
		solve_var_slice_t *depslice = pkghash_get_value(problem->variables_by_uid, uid);
		if (depslice == NULL) {
			dbg(2, "cannot find variable dependency %s", uid);
			continue;
		}

		/* B1 | B2 | ... */
		cnt = 1;
		for (size_t vi = 0; vi < depslice->count; vi++) {
			curvar = &depslice->begin[vi];
			/* Propagate reponame */
			if (curvar->assumed_reponame == NULL) {
				curvar->assumed_reponame = reponame;
			}

			pkg_solve_item_new(rule, curvar, 1);
			cnt++;
		}
	}

	if (cnt == 0) {
		dbg(2, "cannot find any suitable dependency for %s", var->uid);
		pkg_solve_rule_free(rule);
	} else {
		vec_push(&problem->rules, rule);
	}
}

static void
pkg_solve_add_conflict_rule(struct pkg_solve_problem *problem,
    struct pkg *pkg, struct pkg_solve_variable *var,
    struct pkg_conflict *conflict)
{
	const char *uid;
	struct pkg_solve_variable *curvar;
	struct pkg_solve_rule *rule = NULL;
	struct pkg *other;

	uid = conflict->uid;
	solve_var_slice_t *confslice = pkghash_get_value(problem->variables_by_uid, uid);
	if (confslice == NULL) {
		dbg(2, "cannot find conflict %s", uid);
		return;
	}

	/* Add conflict rule from each of the alternative */
	for (size_t vi = 0; vi < confslice->count; vi++) {
		curvar = &confslice->begin[vi];
		other = curvar->unit->pkg;
		if (conflict->type == PKG_CONFLICT_REMOTE_LOCAL) {
			/* Skip inappropriate packages */
			if (pkg->type == PKG_INSTALLED) {
				if (other->type == PKG_INSTALLED)
					continue;
			}
			else {
				if (other->type != PKG_INSTALLED)
					continue;
			}
		}
		else if (conflict->type == PKG_CONFLICT_REMOTE_REMOTE) {
			if (pkg->type == PKG_INSTALLED)
				continue;

			if (other->type == PKG_INSTALLED)
				continue;
		}
		/*
		 * Also if a conflict is digest specific then we skip
		 * variables with mismatched digests
		 */
		if (conflict->digest) {
			if (!STREQ(conflict->digest, other->digest))
				continue;
		}

		/* Conflict rule: (!A | !Bx) must be true */
		rule = pkg_solve_rule_new(PKG_RULE_EXPLICIT_CONFLICT);
		/* !A */
		pkg_solve_item_new(rule, var, -1);
		/* !Bx */
		pkg_solve_item_new(rule, curvar, -1);

		vec_push(&problem->rules, rule);
	}
}

static void
pkg_solve_add_require_rule(struct pkg_solve_problem *problem,
    struct pkg_solve_variable *var, const char *requirement,
    const char *reponame)
{
	struct pkg_solve_rule *rule;
	providev_t *provvec;
	struct pkg *pkg;
	int cnt;

	pkg = var->unit->pkg;

	provvec = pkghash_get_value(problem->j->universe->provides, requirement);
	if (provvec != NULL) {
		dbg(4, "Add require rule: %s-%s(%c) wants %s",
			pkg->name, pkg->version, pkg->type == PKG_INSTALLED ? 'l' : 'r',
			requirement);
		/* Require rule: ( !A | P1 | P2 | P3 ... ) must be true */
		rule = pkg_solve_rule_new(PKG_RULE_REQUIRE);
		/* !A */
		pkg_solve_item_new(rule, var, -1);
		/* P1 | P2 | ... */
		cnt = 1;
		vec_foreach(*provvec, _pi) {
			pkg_solve_handle_provide(problem, &provvec->d[_pi], rule, pkg,
			    reponame, &cnt);
		}

		if (cnt > 1) {
			vec_push(&problem->rules, rule);
		} else {
			/* Missing dependencies... */
			free(rule);
		}
	} else {
		/*
		 * XXX:
		 * This is terribly broken now so ignore till provides/requires
		 * are really fixed.
		 */
		dbg(1, "for package: %s cannot find provide for requirement: %s",
		    pkg->name, requirement);
	}
}

static void
pkg_solve_add_vital_rule(struct pkg_solve_problem *problem,
    solve_var_slice_t *slice)
{
	struct pkg_solve_variable *cur_var, *local_var = NULL, *remote_var = NULL;
	struct pkg_solve_rule *rule = NULL;
	struct pkg *pkg;

	for (size_t vi = 0; vi < slice->count; vi++) {
		cur_var = &slice->begin[vi];
		pkg = cur_var->unit->pkg;

		if (pkg->type == PKG_INSTALLED) {
			local_var = cur_var;
		} else {
			remote_var = cur_var;
		}
	}

	if (local_var && remote_var) {
		/* Vital upgrade rule: ( L | R ) must be true */
		dbg(4, "Add vital rule: want either %s(l) or %s(r)", local_var->unit->pkg->uid, remote_var->unit->pkg->uid);
		rule = pkg_solve_rule_new(PKG_RULE_VITAL);
		/* L */
		pkg_solve_item_new(rule, local_var, 1);
		/* R */
		pkg_solve_item_new(rule, remote_var, 1);
	} else if(local_var) {
		/* Vital keep local rule: ( L ) must be true */
		dbg(4, "Add vital rule: want %s(l) to stay", local_var->unit->pkg->uid);
		rule = pkg_solve_rule_new(PKG_RULE_VITAL);
		/* L */
		pkg_solve_item_new(rule, local_var, 1);
	}

	if (rule)
		vec_push(&problem->rules, rule);
}

static struct pkg_solve_variable *
pkg_solve_find_var_in_chain(solve_var_slice_t *slice,
	struct pkg_job_universe_item *item)
{
	assert(slice != NULL);
	for (size_t vi = 0; vi < slice->count; vi++) {
		struct pkg_solve_variable *cur = &slice->begin[vi];
		if (cur->unit == item) {
			return (cur);
		}
	}

	return (NULL);
}

static int
pkg_solve_add_request_rule(struct pkg_solve_problem *problem,
	struct pkg_solve_variable *var, struct pkg_job_request *req, int inverse)
{
	struct pkg_solve_rule *rule;
	struct pkg_solve_variable *confvar, *curvar;
	int cnt;

	dbg(4, "add variable from %s request with uid %s-%s",
		inverse < 0 ? "delete" : "install", var->uid, var->digest);

	/*
	 * Get the suggested item
	 */
	solve_var_slice_t *reqslice = pkghash_get_value(problem->variables_by_uid, req->items.d[0].pkg->uid);
	var = pkg_solve_find_var_in_chain(reqslice, req->items.d[0].unit);
	assert(var != NULL);
	/* Assume the most significant variable */
	picosat_assume(problem->sat, var->order * inverse);

	/*
	 * Add clause for any of candidates:
	 * A1 | A2 | ... | An
	 */
	rule = pkg_solve_rule_new(PKG_RULE_REQUEST);

	cnt = 0;

	vec_foreach(req->items, _ri) {
		struct pkg_job_request_item *item = &req->items.d[_ri];
		curvar = pkg_solve_find_var_in_chain(reqslice, item->unit);
		assert(curvar != NULL);
		pkg_solve_item_new(rule, curvar, inverse);
		/* All request variables are top level */
		curvar->flags |= PKG_VAR_TOP;

		if (inverse > 0) {
			curvar->flags |= PKG_VAR_INSTALL;
		}

		cnt++;
	}

	if (cnt > 1) {
		vec_push(&problem->rules, rule);
		/* Also need to add pairs of conflicts */
		vec_foreach(req->items, _ri) {
			struct pkg_job_request_item *item = &req->items.d[_ri];
			curvar = pkg_solve_find_var_in_chain(reqslice, item->unit);
			assert(curvar != NULL);
			if (_ri + 1 >= req->items.len)
				continue;
			for (size_t _rj = _ri + 1; _rj < req->items.len; _rj++) {
				struct pkg_job_request_item *confitem = &req->items.d[_rj];
				confvar = pkg_solve_find_var_in_chain(reqslice, confitem->unit);
				assert(confvar != NULL && confvar != curvar && confvar != var);
				/* Conflict rule: (!A | !Bx) must be true */
				rule = pkg_solve_rule_new(PKG_RULE_REQUEST_CONFLICT);
				/* !A */
				pkg_solve_item_new(rule, curvar, -1);
				/* !Bx */
				pkg_solve_item_new(rule, confvar, -1);

				vec_push(&problem->rules, rule);
			}
		}
	}
	else {
		/* No need to add unary rules as we added the assumption already */
		pkg_solve_rule_free(rule);
	}

	var->flags |= PKG_VAR_TOP;
	if (inverse > 0) {
		var->flags |= PKG_VAR_INSTALL;
	}

	return (EPKG_OK);
}

static void
pkg_solve_add_chain_rule(struct pkg_solve_problem *problem,
    solve_var_slice_t *slice)
{
	struct pkg_solve_variable *curvar, *confvar;
	struct pkg_solve_rule *rule;

	for (size_t vi = 0; vi < slice->count; vi++) {
		curvar = &slice->begin[vi];
		/* Conflict rule: (!Ax | !Ay) must be true */
		for (size_t vj = vi + 1; vj < slice->count; vj++) {
			confvar = &slice->begin[vj];
			rule = pkg_solve_rule_new(PKG_RULE_UPGRADE_CONFLICT);
			/* !Ax */
			pkg_solve_item_new(rule, curvar, -1);
			/* !Ay */
			pkg_solve_item_new(rule, confvar, -1);

			vec_push(&problem->rules, rule);
		}
	}
}

static void
pkg_solve_process_universe_variable(struct pkg_solve_problem *problem,
    solve_var_slice_t *slice)
{
	struct pkg_dep *dep;
	struct pkg_conflict *conflict;
	struct pkg *pkg;
	struct pkg_solve_variable *cur_var;
	struct pkg_jobs *j = problem->j;
	struct pkg_job_request *jreq = NULL;
	bool force = j->flags & PKG_FLAG_FORCE;
	bool force_overrides_vital = pkg_object_bool(pkg_config_get("FORCE_CAN_REMOVE_VITAL"));
	bool add_vital = force_overrides_vital ? !force : true;

	for (size_t vi = 0; vi < slice->count; vi++) {
		cur_var = &slice->begin[vi];
		pkg = cur_var->unit->pkg;

		/* Request */
		if (!(cur_var->flags & PKG_VAR_TOP)) {
			jreq = pkghash_get_value(j->request_add, cur_var->uid);
			if (jreq != NULL)
				pkg_solve_add_request_rule(problem, cur_var, jreq, 1);
			jreq = pkghash_get_value(j->request_delete, cur_var->uid);
			if (jreq != NULL)
				pkg_solve_add_request_rule(problem, cur_var, jreq, -1);
		}

		if (jreq) {
			cur_var->assumed_reponame = pkg->reponame;
		}

		/* Depends */
		vec_foreach(pkg->depends, _di) {
			dep = &pkg->depends.d[_di];
			pkg_solve_add_depend_rule(problem, cur_var, dep,
			    cur_var->assumed_reponame);
		}

		/* Conflicts */
		vec_foreach(pkg->conflicts, _ci) {
			conflict = &pkg->conflicts.d[_ci];
			pkg_solve_add_conflict_rule(problem, pkg, cur_var,
			    conflict);
		}

		/* Shlibs */
		vec_foreach(pkg->shlibs_required, i) {
			const char *s = pkg->shlibs_required.d[i];
			/* Ignore 32 bit libraries */
			if (j->ignore_compat32 && str_ends_with(s, ":32"))
				continue;
			if (charv_search(&j->system_shlibs, s) != NULL) {
				/* The shlib is provided by the system */
				continue;
			}
			pkg_solve_add_require_rule(problem, cur_var, s,
			    cur_var->assumed_reponame);
		}
		vec_foreach(pkg->requires, i) {
			pkg_solve_add_require_rule(problem, cur_var,
			    pkg->requires.d[i], cur_var->assumed_reponame);
		}

		/* Vital flag */
		if (pkg->vital && add_vital)
			pkg_solve_add_vital_rule(problem, slice);

		/*
		 * If this var chain contains mutually conflicting vars
		 * we need to register conflicts with all following
		 * vars
		 */
		if (vi == 0 && slice->count > 1) {
			pkg_solve_add_chain_rule(problem, slice);
		}
	}
}

static void
pkg_solve_add_variable(universe_itemv_t *uv,
    struct pkg_solve_problem *problem, size_t *n)
{
	solve_var_slice_t *slice = xcalloc(1, sizeof(*slice));
	vec_foreach(*uv, _i) {
		struct pkg_job_universe_item *ucur = uv->d[_i];
		assert(*n < problem->nvars);
		struct pkg_solve_variable *var = &problem->variables[*n];
		pkg_solve_variable_set(var, ucur);
		if (slice->begin == NULL) {
			slice->begin = var;
			dbg(4, "add variable from universe with uid %s", var->uid);
			pkghash_safe_add(problem->variables_by_uid, var->uid, slice, NULL);
		}
		slice->count++;
		(*n)++;
		var->order = *n;
	}
}

struct pkg_solve_problem *
pkg_solve_jobs_to_sat(struct pkg_jobs *j)
{
	struct pkg_solve_problem *problem;
	universe_itemv_t *uv;
	size_t i = 0;

	problem = xcalloc(1, sizeof(struct pkg_solve_problem));

	problem->j = j;
	problem->nvars = j->universe->nitems;
	problem->variables = xcalloc(problem->nvars, sizeof(struct pkg_solve_variable));
	problem->sat = picosat_init();

	if (problem->sat == NULL) {
		pkg_emit_errno("picosat_init", "pkg_solve_sat_problem");
		pkg_solve_problem_free(problem);
		return (NULL);
	}

	picosat_adjust(problem->sat, problem->nvars);

	/* Parse universe */
	pkghash_foreach(j->universe->items, it) {
		uv = (universe_itemv_t *)it.value;
		/* Add corresponding variables */
		pkg_solve_add_variable(uv, problem, &i);
	}

	/* Add rules for all conflict chains */
	pkghash_foreach(j->universe->items, it) {
		solve_var_slice_t *slice;

		uv = (universe_itemv_t *)it.value;
		slice = pkghash_get_value(problem->variables_by_uid, uv->d[0]->pkg->uid);
		if (slice == NULL) {
			pkg_emit_error("internal solver error: variable %s is not found",
			    uv->d[0]->pkg->uid);
			pkg_solve_problem_free(problem);
			return (NULL);
		}
		pkg_solve_process_universe_variable(problem, slice);
	}

	if (problem->rules.len == 0)
		dbg(1, "problem has no requests");

	return (problem);
}

static int
pkg_solve_picosat_iter(struct pkg_solve_problem *problem, int iter __unused)
{
	size_t i;
	int res;
	struct pkg_solve_variable *var;
	bool is_installed = false;

	picosat_reset_phases(problem->sat);
	picosat_reset_scores(problem->sat);
	/* Set initial guess */
	for (i = 0; i < problem->nvars; i ++) {
		var = &problem->variables[i];
		is_installed = false;

		solve_var_slice_t *pslice = pkghash_get_value(problem->variables_by_uid, var->uid);
		if (pslice != NULL) {
			for (size_t vi = 0; vi < pslice->count; vi++) {
				if (pslice->begin[vi].unit->pkg->type == PKG_INSTALLED) {
					is_installed = true;
					break;
				}
			}
		}

		if (var->flags & PKG_VAR_TOP)
			continue;

		if (!(var->flags & (PKG_VAR_FAILED|PKG_VAR_ASSUMED))) {
			if (is_installed) {
				picosat_set_default_phase_lit(problem->sat, i + 1, 1);
				picosat_set_more_important_lit(problem->sat, i + 1);
			}
			else if (pslice != NULL && pslice->count == 1) {
				/* Prefer not to install if have no local version */
				picosat_set_default_phase_lit(problem->sat, i + 1, -1);
				picosat_set_less_important_lit(problem->sat, i + 1);
			}
		}
		else if (var->flags & PKG_VAR_FAILED) {
			if (var->unit->pkg->type == PKG_INSTALLED) {
				picosat_set_default_phase_lit(problem->sat, i + 1, -1);
				picosat_set_less_important_lit(problem->sat, i + 1);
			}
			else {
				picosat_set_default_phase_lit(problem->sat, i + 1, 1);
				picosat_set_more_important_lit(problem->sat, i + 1);
			}

			var->flags &= ~PKG_VAR_FAILED;
		}
	}

	res = picosat_sat(problem->sat, -1);

	return (res);
}

static void
pkg_solve_set_initial_assumption(struct pkg_solve_problem *problem,
		struct pkg_solve_rule *rule)
{
	struct pkg_job_universe_item *selected, *local;
	struct pkg_solve_item *item;
	struct pkg_solve_variable *var, *cvar;
	bool conservative = false, prefer_local = false;
	const char *assumed_reponame = NULL;

	if (problem->j->type == PKG_JOBS_INSTALL) {
		/* Avoid upgrades on INSTALL job */
		conservative = true;
		prefer_local = true;
	}
	else {
		conservative = pkg_object_bool(pkg_config_get("CONSERVATIVE_UPGRADE"));
	}

	switch (rule->reason) {
	case PKG_RULE_DEPEND:
		/*
		 * The first item is dependent item, the next items are
		 * dependencies. We assume that all deps belong to a single
		 * upgrade chain.
		 */
		assert (rule->items.len > 0);
		item = &rule->items.d[0];
		var = item->var;
		assumed_reponame = var->assumed_reponame;

		/* Check what we are depending on */
		if (!(var->flags & (PKG_VAR_TOP|PKG_VAR_ASSUMED_TRUE))) {
			/*
			 * We are interested merely in dependencies of top variables
			 * or of previously assumed dependencies
			 */
			dbg(4, "not interested in dependencies for %s-%s",
					var->unit->pkg->name, var->unit->pkg->version);
			return;
		}
		else {
			dbg(4, "examine dependencies for %s-%s",
					var->unit->pkg->name, var->unit->pkg->version);
		}


		item = &rule->items.d[1];
		assert (rule->items.len > 1);
		var = item->var;

		/* Look up slice from hash */
		solve_var_slice_t *aslice = pkghash_get_value(problem->variables_by_uid, var->uid);
		assert(aslice != NULL);

		for (size_t vi = 0; vi < aslice->count; vi++) {
			cvar = &aslice->begin[vi];
			if (cvar->flags & PKG_VAR_ASSUMED) {
				/* Do not reassume packages */
				return;
			}
		}

		/* Look up the universe vec to find local package */
		local = NULL;
		{
			universe_itemv_t *uv = pkg_jobs_universe_find(
			    problem->j->universe, var->unit->pkg->uid);
			if (uv != NULL) {
				vec_foreach(*uv, _i) {
					if (uv->d[_i]->pkg->type == PKG_INSTALLED) {
						local = uv->d[_i];
						break;
					}
				}
			}
		}

		if (prefer_local && local != NULL) {
			selected = local;
		}
		else {
			universe_itemv_t *uv = pkg_jobs_universe_find(
			    problem->j->universe, var->unit->pkg->uid);
			selected = pkg_jobs_universe_select_candidate(uv, local,
			    conservative, assumed_reponame, true);

			if (local && (STREQ(selected->pkg->digest, local->pkg->digest) ||
				      !pkg_jobs_need_upgrade(&problem->j->system_shlibs, selected->pkg, local->pkg))) {
				selected = local;
			}
		}

		/* Now we can find the according var */
		if (selected != NULL) {

			for (size_t vi = 0; vi < aslice->count; vi++) {
				cvar = &aslice->begin[vi];
				if (cvar->unit == selected) {
					picosat_set_default_phase_lit(problem->sat, cvar->order, 1);
					dbg(4, "assumed %s-%s(%s) to be installed",
							selected->pkg->name, selected->pkg->version,
							selected->pkg->type == PKG_INSTALLED ? "l" : "r");
					cvar->flags |= PKG_VAR_ASSUMED_TRUE;
				}
				else {
					dbg(4, "assumed %s-%s(%s) to be NOT installed",
							cvar->unit->pkg->name, cvar->unit->pkg->version,
							cvar->unit->pkg->type == PKG_INSTALLED ? "l" : "r");
					picosat_set_default_phase_lit(problem->sat, cvar->order, -1);
				}

				cvar->flags |= PKG_VAR_ASSUMED;
			}

		}
		break;
	case PKG_RULE_REQUIRE:
		/* XXX: deal with require rules somehow */
		break;
	default:
		/* No nothing */
		return;
	}
}

int
pkg_solve_sat_problem(struct pkg_solve_problem *problem)
{
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *item;
	int res, iter = 0;
	size_t i;
	bool need_reiterate = false;
	const int *failed = NULL;
	int attempt = 0;
	struct pkg_solve_variable *var;

	vec_rforeach(problem->rules, j) {
		rule = problem->rules.d[j];

		vec_foreach(rule->items, _ri) {
			item = &rule->items.d[_ri];
			picosat_add(problem->sat, item->var->order * item->inverse);
		}

		picosat_add(problem->sat, 0);
		pkg_debug_print_rule(rule);
	}

	vec_rforeach(problem->rules, j) {
		rule = problem->rules.d[j];
		pkg_solve_set_initial_assumption(problem, rule);
	}

reiterate:

	res = pkg_solve_picosat_iter(problem, iter);

	if (res != PICOSAT_SATISFIABLE) {
		/*
		 * in case we cannot satisfy the problem it appears by
		 * experience that the culprit seems to always be the latest of
		 * listed in the failed assumptions.
		 * So try to remove them for the given problem.
		 * To avoid endless loop allow a maximum of 10 iterations no
		 * more
		 */
		failed = picosat_failed_assumptions(problem->sat);
		attempt++;

		/* get the last failure */
		while (*failed) {
			failed++;
		}
		failed--;

		if (attempt >= 10) {
			pkg_emit_error("Cannot solve problem using SAT solver");
			xstring *sb = xstring_new();

			while (*failed) {
				var = &problem->variables[abs(*failed) - 1];
				vec_rforeach(problem->rules, j) {
					rule = problem->rules.d[j];

					if (rule->reason != PKG_RULE_DEPEND) {
						vec_foreach(rule->items, _ri) {
							item = &rule->items.d[_ri];
							if (item->var == var) {
								pkg_print_rule_buf(rule, sb);
								xstring_putc(sb, '\n');
								break;
							}
						}
					}
				}

				xstring_printf(sb, "cannot %s package %s, remove it from request? ",
						var->flags & PKG_VAR_INSTALL ? "install" : "remove", var->uid);

				xstring_flush(sb);
				if (pkg_emit_query_yesno(true, sb->buf)) {
					var->flags |= PKG_VAR_FAILED;
				}

				failed++;
				need_reiterate = true;
			}
			xstring_free(sb);
		} else {
			var = &problem->variables[abs(*failed) - 1];

			/* Check if the failure is caused by a vital package */
			bool vital_found = false;
			vec_rforeach(problem->rules, vj) {
				struct pkg_solve_rule *vrule = problem->rules.d[vj];
				if (vrule->reason != PKG_RULE_DEPEND)
					continue;
				/* In depend rules, the key element (inverse == -1) is the
				 * dependent package, positive items are its dependencies */
				struct pkg_solve_item *dep_pkg = NULL;
				bool depends_on_var = false;
				vec_foreach(vrule->items, _ri) {
					item = &vrule->items.d[_ri];
					if (item->inverse == -1)
						dep_pkg = item;
					else if (item->var->uid == var->uid ||
					    STREQ(item->var->uid, var->uid))
						depends_on_var = true;
				}
				if (dep_pkg != NULL && depends_on_var &&
				    dep_pkg->var->unit->pkg->vital) {
					pkg_emit_error("Cannot remove %s: "
					    "required by vital package %s",
					    var->uid, dep_pkg->var->uid);
					vital_found = true;
				}
			}
			if (!vital_found)
				pkg_emit_notice("Cannot solve problem using SAT "
				    "solver, trying another plan");

			var->flags |= PKG_VAR_FAILED;

			need_reiterate = true;
		}

	}
	else {

		/* Assign vars */
		for (i = 0; i < problem->nvars; i ++) {
			int val = picosat_deref(problem->sat, i + 1);
			struct pkg_solve_variable *lvar = &problem->variables[i];

			if (val > 0)
				lvar->flags |= PKG_VAR_INSTALL;
			else
				lvar->flags &= ~PKG_VAR_INSTALL;

			dbg(2, "decided %s %s-%s to %s",
					lvar->unit->pkg->type == PKG_INSTALLED ? "local" : "remote",
							lvar->uid, lvar->digest,
							lvar->flags & PKG_VAR_INSTALL ? "install" : "delete");
		}

		/* Check for reiterations */
		if ((problem->j->type == PKG_JOBS_INSTALL ||
				problem->j->type == PKG_JOBS_UPGRADE) && iter == 0) {
			for (i = 0; i < problem->nvars; i ++) {
				bool failed_var = false;
				struct pkg_solve_variable *lvar = &problem->variables[i];

				if (!(lvar->flags & PKG_VAR_INSTALL)) {
					solve_var_slice_t *lslice = pkghash_get_value(problem->variables_by_uid, lvar->uid);
					if (lslice != NULL) {
						for (size_t vi = 0; vi < lslice->count; vi++) {
							struct pkg_solve_variable *cur = &lslice->begin[vi];
							if (cur->flags & PKG_VAR_INSTALL) {
								failed_var = false;
								break;
							}
							else if (cur->unit->pkg->type == PKG_INSTALLED) {
								failed_var = true;
							}
						}
					}
				}

				/*
				 * If we want to delete local packages on installation, do one more SAT
				 * iteration to ensure that we have no other choices
				 */
				if (failed_var) {
					solve_var_slice_t *lslice = pkghash_get_value(problem->variables_by_uid, lvar->uid);
					dbg (1, "trying to delete local package %s-%s on install/upgrade,"
							" reiterate on SAT",
							lvar->unit->pkg->name, lvar->unit->pkg->version);
					need_reiterate = true;

					if (lslice != NULL) {
						for (size_t vi = 0; vi < lslice->count; vi++) {
							lslice->begin[vi].flags |= PKG_VAR_FAILED;
						}
					}
				}
			}
		}
	}

	if (need_reiterate) {
		iter ++;

		/* Restore top-level assumptions */
		for (i = 0; i < problem->nvars; i ++) {
			struct pkg_solve_variable *lvar = &problem->variables[i];

			if (lvar->flags & PKG_VAR_TOP) {
				if (lvar->flags & PKG_VAR_FAILED) {
					lvar->flags ^= PKG_VAR_INSTALL | PKG_VAR_FAILED;
				}

				picosat_assume(problem->sat, lvar->order *
						(lvar->flags & PKG_VAR_INSTALL ? 1 : -1));
			}
		}

		need_reiterate = false;

		goto reiterate;
	}

	return (EPKG_OK);
}

void
pkg_solve_dot_export(struct pkg_solve_problem *problem, FILE *file)
{
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it, *key_elt;
	size_t i;

	fprintf(file, "digraph {\n");

	for (i = 0; i < problem->nvars; i ++) {
		struct pkg_solve_variable *var = &problem->variables[i];

		fprintf(file, "\tp%d [shape=%s label=\"%s-%s\"]\n", var->order,
				var->unit->pkg->type == PKG_INSTALLED ? "ellipse" : "octagon",
				var->uid, var->unit->pkg->version);
	}

	/* Print all variables as nodes */

	vec_rforeach(problem->rules, j) {
		rule = problem->rules.d[j];
		key_elt = NULL;

		switch(rule->reason) {
		case PKG_RULE_DEPEND:
			vec_foreach(rule->items, _ri) {
				it = &rule->items.d[_ri];
				if (it->inverse == -1) {
					key_elt = it;
					break;
				}
			}
			assert (key_elt != NULL);

			vec_foreach(rule->items, _rj) {
				it = &rule->items.d[_rj];
				if (it != key_elt) {
					fprintf(file, "\tp%d -> p%d;\n", key_elt->var->order,
							it->var->order);
				}
			}
			break;
		case PKG_RULE_UPGRADE_CONFLICT:
		case PKG_RULE_EXPLICIT_CONFLICT:
		case PKG_RULE_REQUEST_CONFLICT:
			fprintf(file, "\tp%d -> p%d [arrowhead=none,color=red];\n",
					rule->items.d[0].var->order, rule->items.d[1].var->order);
			break;
		case PKG_RULE_REQUIRE:
			vec_foreach(rule->items, _ri) {
				it = &rule->items.d[_ri];
				if (it->inverse == -1) {
					key_elt = it;
					break;
				}
			}
			assert (key_elt != NULL);

			vec_foreach(rule->items, _rj) {
				it = &rule->items.d[_rj];
				if (it != key_elt) {
					fprintf(file, "\tp%d -> p%d[arrowhead=diamond];\n", key_elt->var->order,
							it->var->order);
				}
			}
			break;
		default:
			break;
		}
	}

	fprintf(file, "}\n");
}

int
pkg_solve_dimacs_export(struct pkg_solve_problem *problem, FILE *f)
{
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it;

	fprintf(f, "p cnf %d %zu\n", (int)problem->nvars, problem->rules.len);

	vec_rforeach(problem->rules, i) {
		rule = problem->rules.d[i];
		vec_foreach(rule->items, _ri) {
			it = &rule->items.d[_ri];
			size_t order = it->var - problem->variables;
			if (order < problem->nvars)
				fprintf(f, "%ld ", (long)((order + 1) * it->inverse));
		}
		fprintf(f, "0\n");
	}
	return (EPKG_OK);
}

/*
 * Check whether a non-requested installed package can safely be kept:
 * - All its dependencies are still satisfied (at least one version of
 *   each dep has PKG_VAR_INSTALL set).
 * - It does not conflict with any package being installed.
 *
 * Returns true if the package can stay.
 */
static bool
pkg_solve_can_keep(struct pkg_solve_problem *problem,
    struct pkg_solve_variable *var)
{
	struct pkg *pkg = var->unit->pkg;
	struct pkg_dep *dep = NULL;
	struct pkg_conflict *conflict = NULL;

	/* Check dependencies */
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		solve_var_slice_t *depslice;

		depslice = pkghash_get_value(problem->variables_by_uid, dep->uid);
		if (depslice == NULL) {
			/*
			 * Dep not in the universe at all -- it is either
			 * already installed outside the solver scope or
			 * genuinely missing.  Assume satisfied.
			 */
			continue;
		}

		/* Check if any version of this dep is being installed/kept */
		bool dep_ok = false;
		for (size_t vi = 0; vi < depslice->count; vi++) {
			struct pkg_solve_variable *cv = &depslice->begin[vi];
			if (cv->flags & PKG_VAR_INSTALL) {
				dep_ok = true;
				break;
			}
		}

		if (!dep_ok) {
			dbg(4, "dep %s of %s-%s not satisfied",
			    dep->uid, pkg->name, pkg->version);
			return (false);
		}
	}

	/* Check conflicts: if any conflicting package is being installed,
	 * this package must be removed */
	vec_foreach(pkg->conflicts, _ci) {
		solve_var_slice_t *confslice;

		conflict = &pkg->conflicts.d[_ci];
		confslice = pkghash_get_value(problem->variables_by_uid,
		    conflict->uid);
		if (confslice == NULL)
			continue;

		for (size_t vi = 0; vi < confslice->count; vi++) {
			struct pkg_solve_variable *cv = &confslice->begin[vi];
			if (cv->flags & PKG_VAR_INSTALL) {
				dbg(4, "%s-%s conflicts with %s-%s being installed",
				    pkg->name, pkg->version,
				    cv->unit->pkg->name,
				    cv->unit->pkg->version);
				return (false);
			}
		}
	}

	return (true);
}

static void
pkg_solve_insert_res_job (solve_var_slice_t *slice,
		struct pkg_solve_problem *problem)
{
	struct pkg_solved *res;
	struct pkg_solve_variable *cur_var, *del_var = NULL, *add_var = NULL;
	int seen_add = 0, seen_del = 0;
	struct pkg_jobs *j = problem->j;

	for (size_t vi = 0; vi < slice->count; vi++) {
		cur_var = &slice->begin[vi];
		if ((cur_var->flags & PKG_VAR_INSTALL) &&
				cur_var->unit->pkg->type != PKG_INSTALLED) {
			add_var = cur_var;
			seen_add ++;
		}
		else if (!(cur_var->flags & PKG_VAR_INSTALL)
				&& cur_var->unit->pkg->type == PKG_INSTALLED) {
			del_var = cur_var;
			seen_del ++;
		}
	}

	if (seen_add > 1) {
		pkg_emit_error("internal solver error: more than two packages to install(%d) "
				"from the same uid: %s", seen_add, slice->begin[0].uid);
		return;
	}
	else if (seen_add != 0 || seen_del != 0) {
		if (seen_add > 0) {
			res = xcalloc(1, sizeof(struct pkg_solved));
			/* Pure install */
			if (seen_del == 0) {
				res->items[0] = add_var->unit;
				res->type = (j->type == PKG_JOBS_FETCH) ?
								PKG_SOLVED_FETCH : PKG_SOLVED_INSTALL;
				vec_push(&j->jobs, res);
				dbg(3, "pkg_solve: schedule installation of %s %s",
					add_var->uid, add_var->digest);
			}
			else {
				/* Upgrade */
				res->items[0] = add_var->unit;
				res->items[1] = del_var->unit;
				res->type = PKG_SOLVED_UPGRADE;
				vec_push(&j->jobs, res);
				dbg(3, "pkg_solve: schedule upgrade of %s from %s to %s",
					del_var->uid, del_var->digest, add_var->digest);
			}
		}

		/*
		 * For delete requests there could be multiple delete requests per UID,
		 * so we need to re-process vars and add all delete jobs required.
		 */
		for (size_t vi = 0; vi < slice->count; vi++) {
			cur_var = &slice->begin[vi];
			if (!(cur_var->flags & PKG_VAR_INSTALL) &&
					cur_var->unit->pkg->type == PKG_INSTALLED) {
				/* Skip already added items */
				if (seen_add > 0 && cur_var == del_var)
					continue;

				/*
				 * For install/upgrade jobs, avoid spurious
				 * removal of packages that entered the universe
				 * via rdeps but whose dependencies are all still
				 * satisfied.  Only allow removal if the package
				 * was explicitly requested for deletion, or if
				 * at least one of its dependencies will no
				 * longer be met.  (issue #2566)
				 */
				if ((j->type == PKG_JOBS_INSTALL ||
				    j->type == PKG_JOBS_UPGRADE) &&
				    pkghash_get(j->request_delete, cur_var->uid) == NULL &&
				    pkg_solve_can_keep(problem, cur_var)) {
					dbg(2, "keeping %s-%s: deps still satisfied",
					    cur_var->unit->pkg->name,
					    cur_var->unit->pkg->version);
					cur_var->flags |= PKG_VAR_INSTALL;
					continue;
				}

				res = xcalloc(1, sizeof(struct pkg_solved));
				res->items[0] = cur_var->unit;
				res->type = PKG_SOLVED_DELETE;
				vec_push(&j->jobs, res);
				dbg(3, "schedule deletion of %s %s",
					cur_var->uid, cur_var->digest);
			}
		}
	}
	else {
		dbg(2, "ignoring package %s(%s) as its state has not been changed",
				slice->begin[0].uid, slice->begin[0].digest);
	}
}

int
pkg_solve_sat_to_jobs(struct pkg_solve_problem *problem)
{
	solve_var_slice_t *slice;
	pkghash_foreach(problem->variables_by_uid, it) {
		slice = (solve_var_slice_t *)it.value;
		dbg(4, "check variable with uid %s", slice->begin[0].uid);
		pkg_solve_insert_res_job(slice, problem);
	}

	return (EPKG_OK);
}

static bool
pkg_solve_parse_sat_output_store(struct pkg_solve_problem *problem, const char *var_str)
{
	struct pkg_solve_variable *var;
	ssize_t order;

	order = strtol(var_str, NULL, 10);
	if (order == 0)
		return (true);
	if (order < 0) {
		/* negative value means false */
		order = - order - 1;
		if ((size_t)order < problem->nvars) {
			var = problem->variables + order;
			var->flags &= ~PKG_VAR_INSTALL;
		}
	} else {
		/* positive value means true */
		order = order - 1;
		if ((size_t)order < problem->nvars) {
			var = problem->variables + order;
			var->flags |= PKG_VAR_INSTALL;
		}
	}
	return (false);
}

int
pkg_solve_parse_sat_output(FILE *f, struct pkg_solve_problem *problem)
{
	int ret = EPKG_OK;
	char *line = NULL, *var_str, *begin;
	size_t linecap = 0;
	bool got_sat = false, done = false;

	while (getline(&line, &linecap, f) > 0) {
		if (strncmp(line, "SAT", 3) == 0) {
			got_sat = true;
		}
		else if (got_sat) {
			begin = line;
			do {
				var_str = strsep(&begin, " \t");
				/* Skip unexpected lines */
				if (var_str == NULL || (!isdigit(*var_str) && *var_str != '-'))
					continue;
				if (pkg_solve_parse_sat_output_store(problem, var_str))
					done = true;
			} while (begin != NULL);
		}
		else if (strncmp(line, "v ", 2) == 0) {
			begin = line + 2;
			do {
				var_str = strsep(&begin, " \t");
				/* Skip unexpected lines */
				if (var_str == NULL || (!isdigit(*var_str) && *var_str != '-'))
					continue;
				if (pkg_solve_parse_sat_output_store(problem, var_str))
					done = true;
			} while (begin != NULL);
		}
		else {
			/* Slightly ignore anything from solver */
			continue;
		}
	}

	if (done)
		ret = pkg_solve_sat_to_jobs(problem);
	else {
		pkg_emit_error("cannot parse sat solver output");
		ret = EPKG_FATAL;
	}

	free(line);

	return (ret);
}
