/*-
 * Copyright (c) 2013-2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <kvec.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"
#include "picosat.h"

struct pkg_solve_item;

enum pkg_solve_rule_type {
	PKG_RULE_DEPEND = 0,
	PKG_RULE_UPGRADE_CONFLICT,
	PKG_RULE_EXPLICIT_CONFLICT,
	PKG_RULE_REQUEST_CONFLICT,
	PKG_RULE_REQUEST,
	PKG_RULE_REQUIRE,
	PKG_RULE_MAX
};

static const char *rule_reasons[] = {
	[PKG_RULE_DEPEND] = "dependency",
	[PKG_RULE_UPGRADE_CONFLICT] = "upgrade",
	[PKG_RULE_REQUEST_CONFLICT] = "candidates",
	[PKG_RULE_EXPLICIT_CONFLICT] = "conflict",
	[PKG_RULE_REQUEST] = "request",
	[PKG_RULE_REQUIRE] = "require",
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
	UT_hash_handle hh;
	struct pkg_solve_variable *next, *prev;
};

struct pkg_solve_item {
	int nitems;
	int nresolved;
	struct pkg_solve_variable *var;
	int inverse;
	struct pkg_solve_item *prev,*next;
};

struct pkg_solve_rule {
	enum pkg_solve_rule_type reason;
	struct pkg_solve_item *items;
};

struct pkg_solve_problem {
	struct pkg_jobs *j;
	kvec_t(struct pkg_solve_rule *) rules;
	struct pkg_solve_variable *variables_by_uid;
	struct pkg_solve_variable *variables;
	PicoSAT *sat;
	size_t nvars;
};

struct pkg_solve_impl_graph {
	struct pkg_solve_variable *var;
	struct pkg_solve_impl_graph *prev, *next;
};

/*
 * Use XOR here to implement the following logic:
 * atom is true if it is installed and not inverted or
 * if it is not installed but inverted
 */
#define PKG_SOLVE_CHECK_ITEM(item)				\
	((item)->var->to_install ^ (item)->inverse)

/*
 * Utilities to convert jobs to SAT rule
 */

static struct pkg_solve_item *
pkg_solve_item_new(struct pkg_solve_variable *var)
{
	struct pkg_solve_item *result;

	result = xcalloc(1, sizeof(struct pkg_solve_item));
	result->var = var;
	result->inverse = 1;

	return (result);
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
	var->prev = var;
}

static void
pkg_solve_rule_free(struct pkg_solve_rule *rule)
{
	struct pkg_solve_item *it, *tmp;

	LL_FOREACH_SAFE(rule->items, it, tmp) {
		free(it);
	}
	free(rule);
}


void
pkg_solve_problem_free(struct pkg_solve_problem *problem)
{
	struct pkg_solve_variable *v, *vtmp;

	while (kv_size(problem->rules)) {
		pkg_solve_rule_free(kv_pop(problem->rules));
	}

	HASH_ITER(hh, problem->variables_by_uid, v, vtmp) {
		HASH_DELETE(hh, problem->variables_by_uid, v);
	}

	picosat_reset(problem->sat);
	free(problem->variables);
	free(problem);
}


#define RULE_ITEM_APPEND(rule, item) do {									\
	(item)->nitems = (rule)->items ? (rule)->items->nitems + 1 : 1;			\
	DL_APPEND((rule)->items, (item));										\
} while (0)

static void
pkg_print_rule_buf(struct pkg_solve_rule *rule, UT_string *sb)
{
	struct pkg_solve_item *it = rule->items, *key_elt = NULL;

	utstring_printf(sb, "%s rule: ", rule_reasons[rule->reason]);
	switch(rule->reason) {
	case PKG_RULE_DEPEND:
		LL_FOREACH(rule->items, it) {
			if (it->inverse == -1) {
				key_elt = it;
				break;
			}
		}
		if (key_elt) {
			utstring_printf(sb, "package %s%s depends on: ", key_elt->var->uid,
				(key_elt->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
		}
		LL_FOREACH(rule->items, it) {
			if (it != key_elt) {
				utstring_printf(sb, "%s%s", it->var->uid,
					(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
			}
		}
		break;
	case PKG_RULE_UPGRADE_CONFLICT:
		utstring_printf(sb, "upgrade local %s-%s to remote %s-%s",
			it->var->uid, it->var->unit->pkg->version,
			it->next->var->uid, it->next->var->unit->pkg->version);
		break;
	case PKG_RULE_EXPLICIT_CONFLICT:
		utstring_printf(sb, "The following packages conflict with each other: ");
		LL_FOREACH(rule->items, it) {
			utstring_printf(sb, "%s-%s%s%s", it->var->unit->pkg->uid, it->var->unit->pkg->version,
				(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)",
				it->next ? ", " : "");
		}
		break;
	case PKG_RULE_REQUIRE:
		LL_FOREACH(rule->items, it) {
			if (it->inverse == -1) {
				key_elt = it;
				break;
			}
		}
		if (key_elt) {
			utstring_printf(sb, "package %s%s depends on a requirement provided by: ",
				key_elt->var->uid,
				(key_elt->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
		}
		LL_FOREACH(rule->items, it) {
			if (it != key_elt) {
				utstring_printf(sb, "%s%s", it->var->uid,
					(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
			}
		}
		break;
	case PKG_RULE_REQUEST_CONFLICT:
		utstring_printf(sb, "The following packages in request are candidates for installation: ");
		LL_FOREACH(rule->items, it) {
			utstring_printf(sb, "%s-%s%s", it->var->uid, it->var->unit->pkg->version,
					it->next ? ", " : "");
		}
		break;
	default:
		break;
	}
}

static void
pkg_debug_print_rule(struct pkg_solve_rule *rule)
{
	UT_string *sb;

	if (debug_level < 3)
		return;

	utstring_new(sb);

	pkg_print_rule_buf(rule, sb);

	pkg_debug(2, "%s", utstring_body(sb));
	utstring_free(sb);
}

static int
pkg_solve_handle_provide (struct pkg_solve_problem *problem,
		struct pkg_job_provide *pr, struct pkg_solve_rule *rule,
		struct pkg *orig, const char *reponame, int *cnt)
{
	struct pkg_solve_item *it = NULL;
	struct pkg_solve_variable *var, *curvar;
	struct pkg_job_universe_item *un;
	struct pkg *pkg;
	bool libfound, providefound;

	/* Find the first package in the universe list */
	un = pr->un;
	while (un->prev->next != NULL) {
		un = un->prev;
	}

	/* Find the corresponding variables chain */
	HASH_FIND_STR(problem->variables_by_uid, un->pkg->uid, var);

	LL_FOREACH(var, curvar) {
		/*
		 * For each provide we need to check whether this package
		 * actually provides this require
		 */
		libfound = providefound = false;
		pkg = curvar->unit->pkg;

		if (pr->is_shlib) {
			libfound = kh_contains(strings, pkg->shlibs_provided, pr->provide);
			/* Skip incompatible ABI as well */
			if (libfound && strcmp(pkg->arch, orig->arch) != 0) {
				pkg_debug(2, "solver: require %s: package %s-%s(%c) provides wrong ABI %s, "
					"wanted %s", pr->provide, pkg->name, pkg->version,
					pkg->type == PKG_INSTALLED ? 'l' : 'r', orig->arch, pkg->arch);
				continue;
			}
		}
		else {
			providefound = kh_contains(strings, pkg->provides, pr->provide);
		}

		if (!providefound && !libfound) {
			pkg_debug(4, "solver: %s provide is not satisfied by %s-%s(%c)", pr->provide,
					pkg->name, pkg->version, pkg->type == PKG_INSTALLED ?
							'l' : 'r');
			continue;
		}

		if (curvar->assumed_reponame == NULL) {
			curvar->assumed_reponame = reponame;
		}

		pkg_debug(4, "solver: %s provide is satisfied by %s-%s(%c)", pr->provide,
				pkg->name, pkg->version, pkg->type == PKG_INSTALLED ?
				'l' : 'r');

		it = pkg_solve_item_new(curvar);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = 1;
		RULE_ITEM_APPEND(rule, it);
		(*cnt) ++;
	}

	return (EPKG_OK);
}

static int
pkg_solve_add_depend_rule(struct pkg_solve_problem *problem,
		struct pkg_solve_variable *var,
		struct pkg_dep *dep,
		const char *reponame)
{
	const char *uid;
	struct pkg_solve_variable *depvar, *curvar;
	struct pkg_solve_rule *rule = NULL;
	struct pkg_solve_item *it = NULL;
	int cnt;

	uid = dep->uid;
	HASH_FIND_STR(problem->variables_by_uid, uid, depvar);
	if (depvar == NULL) {
		pkg_debug(2, "cannot find variable dependency %s", uid);
		return (EPKG_END);
	}
	/* Dependency rule: (!A | B) */
	rule = pkg_solve_rule_new(PKG_RULE_DEPEND);
	if (rule == NULL)
		return (EPKG_FATAL);
	/* !A */
	it = pkg_solve_item_new(var);
	if (it == NULL) {
		pkg_solve_rule_free(rule);
		return (EPKG_FATAL);
	}

	it->inverse = -1;
	RULE_ITEM_APPEND(rule, it);
	/* B1 | B2 | ... */
	cnt = 1;
	LL_FOREACH(depvar, curvar) {
		/* Propagate reponame */
		if (curvar->assumed_reponame == NULL) {
			curvar->assumed_reponame = reponame;
		}

		it = pkg_solve_item_new(curvar);
		if (it == NULL) {
			pkg_solve_rule_free(rule);
			return (EPKG_FATAL);
		}

		it->inverse = 1;
		RULE_ITEM_APPEND(rule, it);
		cnt ++;
	}

	kv_prepend(typeof(rule), problem->rules, rule);

	return (EPKG_OK);
}

static int
pkg_solve_add_conflict_rule(struct pkg_solve_problem *problem,
		struct pkg *pkg,
		struct pkg_solve_variable *var,
		struct pkg_conflict *conflict)
{
	const char *uid;
	struct pkg_solve_variable *confvar, *curvar;
	struct pkg_solve_rule *rule = NULL;
	struct pkg_solve_item *it = NULL;
	struct pkg *other;

	uid = conflict->uid;
	HASH_FIND_STR(problem->variables_by_uid, uid, confvar);
	if (confvar == NULL) {
		pkg_debug(2, "cannot find conflict %s", uid);
		return (EPKG_END);
	}

	/* Add conflict rule from each of the alternative */
	LL_FOREACH(confvar, curvar) {
		other = curvar->unit->pkg;
		if (conflict->type == PKG_CONFLICT_REMOTE_LOCAL) {
			/* Skip unappropriate packages */
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
			if (strcmp (conflict->digest, other->digest) != 0)
				continue;
		}

		/* Conflict rule: (!A | !Bx) */
		rule = pkg_solve_rule_new(PKG_RULE_EXPLICIT_CONFLICT);
		if (rule == NULL)
			return (EPKG_FATAL);
		/* !A */
		it = pkg_solve_item_new(var);
		if (it == NULL) {
			pkg_solve_rule_free(rule);
			return (EPKG_FATAL);
		}

		it->inverse = -1;
		RULE_ITEM_APPEND(rule, it);
		/* !Bx */
		it = pkg_solve_item_new(curvar);
		if (it == NULL) {
			pkg_solve_rule_free(rule);
			return (EPKG_FATAL);
		}

		it->inverse = -1;
		RULE_ITEM_APPEND(rule, it);

		kv_prepend(typeof(rule), problem->rules, rule);
	}

	return (EPKG_OK);
}

static int
pkg_solve_add_require_rule(struct pkg_solve_problem *problem,
		struct pkg_solve_variable *var,
		const char *requirement,
		const char *reponame)
{
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it = NULL;
	struct pkg_job_provide *pr, *prhead;
	struct pkg *pkg;
	int cnt;

	pkg = var->unit->pkg;

	HASH_FIND_STR(problem->j->universe->provides, requirement, prhead);
	if (prhead != NULL) {
		pkg_debug(4, "solver: Add require rule: %s-%s(%c) wants %s",
			pkg->name, pkg->version, pkg->type == PKG_INSTALLED ? 'l' : 'r',
			requirement);
		/* Require rule !A | P1 | P2 | P3 ... */
		rule = pkg_solve_rule_new(PKG_RULE_REQUIRE);
		if (rule == NULL)
			return (EPKG_FATAL);
		/* !A */
		it = pkg_solve_item_new(var);
		if (it == NULL) {
			pkg_solve_rule_free(rule);
			return (EPKG_FATAL);
		}

		it->inverse = -1;
		RULE_ITEM_APPEND(rule, it);
		/* B1 | B2 | ... */
		cnt = 1;
		LL_FOREACH(prhead, pr) {
			if (pkg_solve_handle_provide(problem, pr, rule, pkg, reponame, &cnt)
					!= EPKG_OK) {
				free(it);
				free(rule);
				return (EPKG_FATAL);
			}
		}

		if (cnt > 1) {
			kv_prepend(typeof(rule), problem->rules, rule);
		}
		else {
			/* Missing dependencies... */
			free(it);
			free(rule);
		}
	}
	else {
		/*
		 * XXX:
		 * This is terribly broken now so ignore till provides/requires
		 * are really fixed.
		 */
		pkg_debug(1, "solver: for package: %s cannot find provide for requirement: %s",
		    pkg->name, requirement);
	}

	return (EPKG_OK);
}

static struct pkg_solve_variable *
pkg_solve_find_var_in_chain(struct pkg_solve_variable *var,
	struct pkg_job_universe_item *item)
{
	struct pkg_solve_variable *cur;

	assert(var != NULL);
	LL_FOREACH(var, cur) {
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
	struct pkg_solve_rule *rule = NULL;
	struct pkg_solve_item *it = NULL;
	struct pkg_job_request_item *item, *confitem;
	struct pkg_solve_variable *confvar, *curvar;
	int cnt;

	pkg_debug(4, "solver: add variable from %s request with uid %s-%s",
		inverse < 0 ? "delete" : "install", var->uid, var->digest);

	/*
	 * Get the suggested item
	 */
	HASH_FIND_STR(problem->variables_by_uid, req->item->pkg->uid, var);
	var = pkg_solve_find_var_in_chain(var, req->item->unit);
	assert(var != NULL);
	/* Assume the most significant variable */
	picosat_assume(problem->sat, var->order * inverse);

	/*
	 * Add clause for any of candidates:
	 * A1 | A2 | ... | An
	 */
	rule = pkg_solve_rule_new(PKG_RULE_REQUEST);
	if (rule == NULL)
		return (EPKG_FATAL);

	cnt = 0;

	LL_FOREACH(req->item, item) {
		curvar = pkg_solve_find_var_in_chain(var, item->unit);
		assert(curvar != NULL);
		it = pkg_solve_item_new(curvar);
		if (it == NULL) {
			pkg_solve_rule_free(rule);
			return (EPKG_FATAL);
		}

		/* All request variables are top level */
		curvar->flags |= PKG_VAR_TOP;

		if (inverse > 0) {
			curvar->flags |= PKG_VAR_INSTALL;
		}

		it->inverse = inverse;
		RULE_ITEM_APPEND(rule, it);
		cnt ++;
	}

	if (cnt > 1 && var->unit->hh.keylen != 0) {
		kv_prepend(typeof(rule), problem->rules, rule);
		/* Also need to add pairs of conflicts */
		LL_FOREACH(req->item, item) {
			curvar = pkg_solve_find_var_in_chain(var, item->unit);
			assert(curvar != NULL);
			if (item->next == NULL)
				continue;
			LL_FOREACH(item->next, confitem) {
				confvar = pkg_solve_find_var_in_chain(var, confitem->unit);
				assert(confvar != NULL && confvar != curvar && confvar != var);
				/* Conflict rule: (!A | !Bx) */
				rule = pkg_solve_rule_new(PKG_RULE_REQUEST_CONFLICT);
				if (rule == NULL)
					return (EPKG_FATAL);
				/* !A */
				it = pkg_solve_item_new(curvar);
				if (it == NULL) {
					pkg_solve_rule_free(rule);
					return (EPKG_FATAL);
				}

				it->inverse = -1;
				RULE_ITEM_APPEND(rule, it);
				/* !Bx */
				it = pkg_solve_item_new(confvar);
				if (it == NULL) {
					pkg_solve_rule_free(rule);
					return (EPKG_FATAL);
				}

				it->inverse = -1;
				RULE_ITEM_APPEND(rule, it);

				kv_prepend(typeof(rule), problem->rules, rule);
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

static int
pkg_solve_add_chain_rule(struct pkg_solve_problem *problem,
	struct pkg_solve_variable *var)
{
	struct pkg_solve_variable *curvar, *confvar;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it = NULL;

	/* Rewind to first */
	while (var->prev->next != NULL) {
		var = var->prev;
	}

	LL_FOREACH(var, curvar) {
		/* Conflict rule: (!Ax | !Ay) */
		if (curvar->next == NULL) {
			break;
		}

		LL_FOREACH(curvar->next, confvar) {
			rule = pkg_solve_rule_new(PKG_RULE_UPGRADE_CONFLICT);
			if (rule == NULL)
				return (EPKG_FATAL);
			/* !Ax */
			it = pkg_solve_item_new(curvar);
			if (it == NULL) {
				pkg_solve_rule_free(rule);
				return (EPKG_FATAL);
			}

			it->inverse = -1;
			RULE_ITEM_APPEND(rule, it);
			/* !Ay */
			it = pkg_solve_item_new(confvar);
			if (it == NULL) {
				pkg_solve_rule_free(rule);
				return (EPKG_FATAL);
			}

			it->inverse = -1;
			RULE_ITEM_APPEND(rule, it);

			kv_prepend(typeof(rule), problem->rules, rule);
		}
	}

	return (EPKG_OK);
}

static int
pkg_solve_process_universe_variable(struct pkg_solve_problem *problem,
		struct pkg_solve_variable *var)
{
	struct pkg_dep *dep;
	struct pkg_conflict *conflict;
	struct pkg *pkg;
	struct pkg_solve_variable *cur_var;
	struct pkg_jobs *j = problem->j;
	struct pkg_job_request *jreq = NULL;
	char *buf;
	bool chain_added = false;

	LL_FOREACH(var, cur_var) {
		pkg = cur_var->unit->pkg;

		/* Request */
		if (!(cur_var->flags & PKG_VAR_TOP)) {
			HASH_FIND_STR(j->request_add, cur_var->uid, jreq);
			if (jreq != NULL)
				pkg_solve_add_request_rule(problem, cur_var, jreq, 1);
			HASH_FIND_STR(j->request_delete, cur_var->uid, jreq);
			if (jreq != NULL)
				pkg_solve_add_request_rule(problem, cur_var, jreq, -1);
		}

		if (jreq) {
			cur_var->assumed_reponame = pkg->reponame;
		}

		/* Depends */
		LL_FOREACH(pkg->depends, dep) {
			if (pkg_solve_add_depend_rule(problem, cur_var, dep,
					cur_var->assumed_reponame) != EPKG_OK) {
				continue;
			}
		}

		/* Conflicts */
		LL_FOREACH(pkg->conflicts, conflict) {
			if (pkg_solve_add_conflict_rule(problem, pkg, cur_var, conflict) !=
							EPKG_OK)
				continue;
		}

		/* Shlibs */
		buf = NULL;
		while (pkg_shlibs_required(pkg, &buf) == EPKG_OK) {
			if (pkg_solve_add_require_rule(problem, cur_var,
					buf, cur_var->assumed_reponame) != EPKG_OK) {
				continue;
			}
		}
		buf = NULL;
		while (pkg_requires(pkg, &buf) == EPKG_OK) {
			if (pkg_solve_add_require_rule(problem, cur_var,
					buf, cur_var->assumed_reponame) != EPKG_OK) {
				continue;
			}
		}


		/*
		 * If this var chain contains mutually conflicting vars
		 * we need to register conflicts with all following
		 * vars
		 */
		if (!chain_added && (cur_var->next != NULL || cur_var->prev != var)) {
			if (pkg_solve_add_chain_rule(problem, cur_var) != EPKG_OK)
				continue;

			chain_added = true;
		}
	}

	return (EPKG_OK);
}

static int
pkg_solve_add_variable(struct pkg_job_universe_item *un,
		struct pkg_solve_problem *problem, size_t *n)
{
	struct pkg_job_universe_item *ucur;
	struct pkg_solve_variable *var = NULL, *tvar = NULL;

	LL_FOREACH(un, ucur) {
		assert(*n < problem->nvars);

		/* Add new variable */
		var = &problem->variables[*n];
		pkg_solve_variable_set(var, ucur);

		if (tvar == NULL) {
			pkg_debug(4, "solver: add variable from universe with uid %s", var->uid);
			HASH_ADD_KEYPTR(hh, problem->variables_by_uid,
				var->uid, strlen(var->uid), var);
			tvar = var;
		}
		else {
			/* Insert a variable to a chain */
			DL_APPEND(tvar, var);
		}
		(*n) ++;
		var->order = *n;
	}

	return (EPKG_OK);
}

struct pkg_solve_problem *
pkg_solve_jobs_to_sat(struct pkg_jobs *j)
{
	struct pkg_solve_problem *problem;
	struct pkg_job_universe_item *un, *utmp;
	size_t i = 0;

	problem = xcalloc(1, sizeof(struct pkg_solve_problem));

	problem->j = j;
	problem->nvars = j->universe->nitems;
	problem->variables = xcalloc(problem->nvars, sizeof(struct pkg_solve_variable));
	problem->sat = picosat_init();
	kv_init(problem->rules);

	if (problem->sat == NULL) {
		pkg_emit_errno("picosat_init", "pkg_solve_sat_problem");
		return (NULL);
	}

	picosat_adjust(problem->sat, problem->nvars);

	/* Parse universe */
	HASH_ITER(hh, j->universe->items, un, utmp) {
		/* Add corresponding variables */
		if (pkg_solve_add_variable(un, problem, &i)
						== EPKG_FATAL)
			goto err;
	}

	/* Add rules for all conflict chains */
	HASH_ITER(hh, j->universe->items, un, utmp) {
		struct pkg_solve_variable *var;

		HASH_FIND_STR(problem->variables_by_uid, un->pkg->uid, var);
		if (var == NULL) {
			pkg_emit_error("internal solver error: variable %s is not found",
			    un->pkg->uid);
			goto err;
		}
		if (pkg_solve_process_universe_variable(problem, var) != EPKG_OK)
			goto err;
	}

	if (kv_size(problem->rules) == 0) {
		pkg_debug(1, "problem has no requests");
		return (problem);
	}

	return (problem);

err:
	return (NULL);
}

static int
pkg_solve_picosat_iter(struct pkg_solve_problem *problem, int iter)
{
	int res, i;
	struct pkg_solve_variable *var, *cur;
	bool is_installed = false;

	picosat_reset_phases(problem->sat);
	picosat_reset_scores(problem->sat);
	/* Set initial guess */
	for (i = 0; i < problem->nvars; i ++) {
		var = &problem->variables[i];
		is_installed = false;

		LL_FOREACH(var, cur) {
			if (cur->unit->pkg->type == PKG_INSTALLED) {
				is_installed = true;
				break;
			}
		}

		if (var->flags & PKG_VAR_TOP)
			continue;

		if (!(var->flags & (PKG_VAR_FAILED|PKG_VAR_ASSUMED))) {
			if (is_installed) {
				picosat_set_default_phase_lit(problem->sat, i + 1, 1);
				picosat_set_more_important_lit(problem->sat, i + 1);
			}
			else if  (!var->next && var->prev == var) {
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
	struct pkg_job_universe_item *selected, *cur, *local, *first;
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
		assert (rule->items != NULL);
		item = rule->items;
		var = item->var;
		assumed_reponame = var->assumed_reponame;

		/* Check what we are depending on */
		if (!(var->flags & (PKG_VAR_TOP|PKG_VAR_ASSUMED_TRUE))) {
			/*
			 * We are interested merely in dependencies of top variables
			 * or of previously assumed dependencies
			 */
			pkg_debug(4, "solver: not interested in dependencies for %s-%s",
					var->unit->pkg->name, var->unit->pkg->version);
			return;
		}
		else {
			pkg_debug(4, "solver: examine dependencies for %s-%s",
					var->unit->pkg->name, var->unit->pkg->version);
		}


		item = rule->items->next;
		assert (item != NULL);
		var = item->var;
		first = var->unit;

		/* Rewind chains */
		while (first->prev->next != NULL) {
			first = first->prev;
		}
		while (var->prev->next != NULL) {
			var = var->prev;
		}

		LL_FOREACH(var, cvar) {
			if (cvar->flags & PKG_VAR_ASSUMED) {
				/* Do not reassume packages */
				return;
			}
		}
		/* Forward chain to find local package */
		local = NULL;

		DL_FOREACH (first, cur) {
			if (cur->pkg->type == PKG_INSTALLED) {
				local = cur;
				break;
			}
		}

		if (prefer_local && local != NULL) {
			selected = local;
		}
		else {
			selected = pkg_jobs_universe_select_candidate(first, local,
			    conservative, assumed_reponame, true);

			if (local && strcmp (selected->pkg->digest, local->pkg->digest) == 0) {
				selected = local;
			}
		}

		/* Now we can find the according var */
		if (selected != NULL) {

			LL_FOREACH(var, cvar) {
				if (cvar->unit == selected) {
					picosat_set_default_phase_lit(problem->sat, cvar->order, 1);
					pkg_debug(4, "solver: assumed %s-%s(%s) to be installed",
							selected->pkg->name, selected->pkg->version,
							selected->pkg->type == PKG_INSTALLED ? "l" : "r");
					cvar->flags |= PKG_VAR_ASSUMED_TRUE;
				}
				else {
					pkg_debug(4, "solver: assumed %s-%s(%s) to be NOT installed",
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

	for (i = 0; i < kv_size(problem->rules); i++) {
		rule = kv_A(problem->rules, i);

		LL_FOREACH(rule->items, item) {
			picosat_add(problem->sat, item->var->order * item->inverse);
		}

		picosat_add(problem->sat, 0);
		pkg_debug_print_rule(rule);
	}

	for (i = 0; i < kv_size(problem->rules); i++) {
		rule = kv_A(problem->rules, i);
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
			UT_string *sb;
			utstring_new(sb);

			while (*failed) {
				var = &problem->variables[abs(*failed) - 1];
				for (i = 0; i < kv_size(problem->rules); i++) {
					rule = kv_A(problem->rules, i);

					if (rule->reason != PKG_RULE_DEPEND) {
						LL_FOREACH(rule->items, item) {
							if (item->var == var) {
								pkg_print_rule_buf(rule, sb);
								utstring_printf(sb, "%c", '\n');
								break;
							}
						}
					}
				}

				utstring_printf(sb, "cannot %s package %s, remove it from request? ",
						var->flags & PKG_VAR_INSTALL ? "install" : "remove", var->uid);

				if (pkg_emit_query_yesno(true, utstring_body(sb))) {
					var->flags |= PKG_VAR_FAILED;
				}

				failed++;
				need_reiterate = true;
			}
			utstring_clear(sb);
		} else {
			pkg_emit_notice("Cannot solve problem using SAT solver, trying another plan");
			var = &problem->variables[abs(*failed) - 1];

			var->flags |= PKG_VAR_FAILED;

			need_reiterate = true;
		}

#if 0
		failed = picosat_next_maximal_satisfiable_subset_of_assumptions(problem->sat);

		while (*failed) {
			struct pkg_solve_variable *var = &problem->variables[*failed - 1];

			pkg_emit_notice("var: %s", var->uid);

			failed ++;
		}

		return (EPKG_AGAIN);
#endif
	}
	else {

		/* Assign vars */
		for (i = 0; i < problem->nvars; i ++) {
			int val = picosat_deref(problem->sat, i + 1);
			struct pkg_solve_variable *var = &problem->variables[i];

			if (val > 0)
				var->flags |= PKG_VAR_INSTALL;
			else
				var->flags &= ~PKG_VAR_INSTALL;

			pkg_debug(2, "decided %s %s-%s to %s",
					var->unit->pkg->type == PKG_INSTALLED ? "local" : "remote",
							var->uid, var->digest,
							var->flags & PKG_VAR_INSTALL ? "install" : "delete");
		}

		/* Check for reiterations */
		if ((problem->j->type == PKG_JOBS_INSTALL ||
				problem->j->type == PKG_JOBS_UPGRADE) && iter == 0) {
			for (i = 0; i < problem->nvars; i ++) {
				bool failed_var = false;
				struct pkg_solve_variable *var = &problem->variables[i], *cur;

				if (!(var->flags & PKG_VAR_INSTALL)) {
					LL_FOREACH(var, cur) {
						if (cur->flags & PKG_VAR_INSTALL) {
							failed_var = false;
							break;
						}
						else if (cur->unit->pkg->type == PKG_INSTALLED) {
							failed_var = true;
						}
					}
				}

				/*
				 * If we want to delete local packages on installation, do one more SAT
				 * iteration to ensure that we have no other choices
				 */
				if (failed_var) {
					pkg_debug (1, "trying to delete local package %s-%s on install/upgrade,"
							" reiterate on SAT",
							var->unit->pkg->name, var->unit->pkg->version);
					need_reiterate = true;

					LL_FOREACH(var, cur) {
						cur->flags |= PKG_VAR_FAILED;
					}
				}
			}
		}
	}

	if (need_reiterate) {
		iter ++;

		/* Restore top-level assumptions */
		for (i = 0; i < problem->nvars; i ++) {
			struct pkg_solve_variable *var = &problem->variables[i];

			if (var->flags & PKG_VAR_TOP) {
				if (var->flags & PKG_VAR_FAILED) {
					var->flags ^= PKG_VAR_INSTALL | PKG_VAR_FAILED;
				}

				picosat_assume(problem->sat, var->order *
						(var->flags & PKG_VAR_INSTALL ? 1 : -1));
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
	size_t i;

	fprintf(file, "digraph {\n");

	for (i = 0; i < problem->nvars; i ++) {
		struct pkg_solve_variable *var = &problem->variables[i];

		fprintf(file, "\tp%d [shape=%s label=\"%s-%s\"]\n", var->order,
				var->unit->pkg->type == PKG_INSTALLED ? "ellipse" : "octagon",
				var->uid, var->unit->pkg->version);
	}

	/* Print all variables as nodes */

	for (i = 0; i < kv_size(problem->rules); i++) {
		rule = kv_A(problem->rules, i);
		struct pkg_solve_item *it = rule->items, *key_elt = NULL;

		switch(rule->reason) {
		case PKG_RULE_DEPEND:
			LL_FOREACH(rule->items, it) {
				if (it->inverse == -1) {
					key_elt = it;
					break;
				}
			}
			assert (key_elt != NULL);

			LL_FOREACH(rule->items, it) {
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
					it->var->order, it->next->var->order);
			break;
		case PKG_RULE_REQUIRE:
			LL_FOREACH(rule->items, it) {
				if (it->inverse == -1) {
					key_elt = it;
					break;
				}
			}
			assert (key_elt != NULL);

			LL_FOREACH(rule->items, it) {
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

	fprintf(f, "p cnf %d %zu\n", (int)problem->nvars, kv_size(problem->rules));

	for (unsigned int i = 0; i < kv_size(problem->rules); i++) {
		rule = kv_A(problem->rules, i);
		LL_FOREACH(rule->items, it) {
			size_t order = it->var - problem->variables;
			if (order < problem->nvars)
				fprintf(f, "%ld ", (long)((order + 1) * it->inverse));
		}
		fprintf(f, "0\n");
	}
	return (EPKG_OK);
}

static void
pkg_solve_insert_res_job (struct pkg_solve_variable *var,
		struct pkg_solve_problem *problem)
{
	struct pkg_solved *res;
	struct pkg_solve_variable *cur_var, *del_var = NULL, *add_var = NULL;
	int seen_add = 0, seen_del = 0;
	struct pkg_jobs *j = problem->j;

	LL_FOREACH(var, cur_var) {
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
				"from the same uid: %s", seen_add, var->uid);
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
				DL_APPEND(j->jobs, res);
				pkg_debug(3, "pkg_solve: schedule installation of %s %s",
					add_var->uid, add_var->digest);
			}
			else {
				/* Upgrade */
				res->items[0] = add_var->unit;
				res->items[1] = del_var->unit;
				res->type = PKG_SOLVED_UPGRADE;
				DL_APPEND(j->jobs, res);
				pkg_debug(3, "pkg_solve: schedule upgrade of %s from %s to %s",
					del_var->uid, del_var->digest, add_var->digest);
			}
			j->count ++;
		}

		/*
		 * For delete requests there could be multiple delete requests per UID,
		 * so we need to re-process vars and add all delete jobs required.
		 */
		LL_FOREACH(var, cur_var) {
			if (!(cur_var->flags & PKG_VAR_INSTALL) &&
					cur_var->unit->pkg->type == PKG_INSTALLED) {
				/* Skip already added items */
				if (seen_add > 0 && cur_var == del_var)
					continue;

				res = xcalloc(1, sizeof(struct pkg_solved));
				res->items[0] = cur_var->unit;
				res->type = PKG_SOLVED_DELETE;
				DL_APPEND(j->jobs, res);
				pkg_debug(3, "pkg_solve: schedule deletion of %s %s",
					cur_var->uid, cur_var->digest);
				j->count ++;
			}
		}
	}
	else {
		pkg_debug(2, "solver: ignoring package %s(%s) as its state has not been changed",
				var->uid, var->digest);
	}
}

int
pkg_solve_sat_to_jobs(struct pkg_solve_problem *problem)
{
	struct pkg_solve_variable *var, *tvar;

	HASH_ITER(hh, problem->variables_by_uid, var, tvar) {
		pkg_debug(4, "solver: check variable with uid %s", var->uid);
		pkg_solve_insert_res_job(var, problem);
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
	ssize_t linelen;
	bool got_sat = false, done = false;

	while ((linelen = getline(&line, &linecap, f)) > 0) {
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
