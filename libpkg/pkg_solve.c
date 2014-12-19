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
#define _WITH_GETLINE
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

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

struct pkg_solve_variable {
	struct pkg_job_universe_item *unit;
	bool to_install;
	bool top_level;
	int priority;
	const char *digest;
	const char *uid;
	int order;
	UT_hash_handle hh;
	struct pkg_solve_variable *next, *prev;
};

struct pkg_solve_item {
	int nitems;
	int nresolved;
	struct pkg_solve_variable *var;
	int inverse;
	struct pkg_solve_item *next;
};

struct pkg_solve_rule {
	enum pkg_solve_rule_type reason;
	struct pkg_solve_item *items;
	struct pkg_solve_rule *next;
};

struct pkg_solve_problem {
	struct pkg_jobs *j;
	unsigned int rules_count;
	struct pkg_solve_rule *rules;
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

#define PKG_SOLVE_VAR_NEXT(a, e) ((e) == NULL ? &a[0] : (e + 1))

/*
 * Utilities to convert jobs to SAT rule
 */

static struct pkg_solve_item *
pkg_solve_item_new(struct pkg_solve_variable *var)
{
	struct pkg_solve_item *result;

	result = calloc(1, sizeof(struct pkg_solve_item));

	if(result == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_item");
		return (NULL);
	}

	result->var = var;
	result->inverse = 1;

	return (result);
}

static struct pkg_solve_rule *
pkg_solve_rule_new(enum pkg_solve_rule_type reason)
{
	struct pkg_solve_rule *result;

	result = calloc(1, sizeof(struct pkg_solve_rule));

	if(result == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_rule");
		return (NULL);
	}

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
	struct pkg_solve_rule *r, *rtmp;
	struct pkg_solve_variable *v, *vtmp;

	LL_FOREACH_SAFE(problem->rules, r, rtmp) {
		pkg_solve_rule_free(r);
	}

	HASH_ITER(hh, problem->variables_by_uid, v, vtmp) {
		HASH_DELETE(hh, problem->variables_by_uid, v);
	}

	picosat_reset(problem->sat);
	free(problem->variables);
	free(problem);
}


#define RULE_ITEM_PREPEND(rule, item) do {									\
	(item)->nitems = (rule)->items ? (rule)->items->nitems + 1 : 1;			\
	LL_PREPEND((rule)->items, (item));										\
} while (0)

static void
pkg_print_rule_sbuf(struct pkg_solve_rule *rule, struct sbuf *sb)
{
	struct pkg_solve_item *it = rule->items, *key_elt = NULL;

	sbuf_printf(sb, "%s rule: ", rule_reasons[rule->reason]);
	switch(rule->reason) {
	case PKG_RULE_DEPEND:
		LL_FOREACH(rule->items, it) {
			if (it->inverse) {
				key_elt = it;
				break;
			}
		}
		if (key_elt) {
			sbuf_printf(sb, "package %s%s depends on: ", key_elt->var->uid,
				(key_elt->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
		}
		LL_FOREACH(rule->items, it) {
			if (it != key_elt) {
				sbuf_printf(sb, "%s%s", it->var->uid,
					(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
			}
		}
		break;
	case PKG_RULE_UPGRADE_CONFLICT:
		sbuf_printf(sb, "upgrade local %s-%s to remote %s-%s",
			it->next->var->uid, it->next->var->unit->pkg->version,
			it->var->uid, it->var->unit->pkg->version);
		break;
	case PKG_RULE_EXPLICIT_CONFLICT:
		sbuf_printf(sb, "The following packages conflict with each other: ");
		LL_FOREACH(rule->items, it) {
			sbuf_printf(sb, "%s-%s%s%s", it->var->unit->pkg->uid, it->var->unit->pkg->version,
				(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)",
				it->next ? ", " : "");
		}
		break;
	case PKG_RULE_REQUIRE:
		LL_FOREACH(rule->items, it) {
			if (it->inverse) {
				key_elt = it;
				break;
			}
		}
		if (key_elt) {
			sbuf_printf(sb, "package %s%s depends on shared library provided by: ",
				key_elt->var->uid,
				(key_elt->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
		}
		LL_FOREACH(rule->items, it) {
			if (it != key_elt) {
				sbuf_printf(sb, "%s%s", it->var->uid,
					(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)");
			}
		}
		break;
	case PKG_RULE_REQUEST_CONFLICT:
		sbuf_printf(sb, "The following packages in request are candidates for installation: ");
		LL_FOREACH(rule->items, it) {
			sbuf_printf(sb, "%s-%s%s", it->var->uid, it->var->unit->pkg->version,
					it->next ? ", " : "");
		}
		break;
	default:
		break;
	}

	sbuf_finish(sb);
}

static void
pkg_debug_print_rule(struct pkg_solve_rule *rule)
{
	struct sbuf *sb;

	if (debug_level < 3)
		return;

	sb = sbuf_new_auto();

	pkg_print_rule_sbuf(rule, sb);

	pkg_debug(2, "%s", sbuf_data(sb));
	sbuf_delete(sb);
}

static int
pkg_solve_handle_provide (struct pkg_solve_problem *problem,
		struct pkg_job_provide *pr, struct pkg_solve_rule *rule, int *cnt)
{
	struct pkg_solve_item *it = NULL;
	struct pkg_solve_variable *var, *curvar;
	struct pkg_job_universe_item *un;

	/* Find the first package in the universe list */
	un = pr->un;
	while (un->prev->next != NULL) {
		un = un->prev;
	}

	/* Find the corresponding variables chain */
	HASH_FIND_STR(problem->variables_by_uid, un->pkg->uid, var);

	LL_FOREACH(var, curvar) {
		/* For each provide */
		it = pkg_solve_item_new(curvar);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = 1;
		RULE_ITEM_PREPEND(rule, it);
		(*cnt) ++;
	}

	return (EPKG_OK);
}

static int
pkg_solve_add_depend_rule(struct pkg_solve_problem *problem,
		struct pkg_solve_variable *var,
		struct pkg_dep *dep)
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
	if (it == NULL)
		return (EPKG_FATAL);

	it->inverse = -1;
	RULE_ITEM_PREPEND(rule, it);
	/* B1 | B2 | ... */
	cnt = 1;
	LL_FOREACH(depvar, curvar) {
		it = pkg_solve_item_new(curvar);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = 1;
		RULE_ITEM_PREPEND(rule, it);
		cnt ++;
	}

	LL_PREPEND(problem->rules, rule);
	problem->rules_count ++;

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

	uid = conflict->uid;
	HASH_FIND_STR(problem->variables_by_uid, uid, confvar);
	if (confvar == NULL) {
		pkg_debug(2, "cannot find conflict %s", uid);
		return (EPKG_END);
	}

	/* Add conflict rule from each of the alternative */
	LL_FOREACH(confvar, curvar) {
		if (conflict->type == PKG_CONFLICT_REMOTE_LOCAL) {
			/* Skip unappropriate packages */
			if (pkg->type == PKG_INSTALLED) {
				if (curvar->unit->pkg->type == PKG_INSTALLED)
					continue;
			}
			else {
				if (curvar->unit->pkg->type != PKG_INSTALLED)
					continue;
			}
		}
		else if (conflict->type == PKG_CONFLICT_REMOTE_REMOTE) {
			if (pkg->type == PKG_INSTALLED)
				continue;

			if (curvar->unit->pkg->type == PKG_INSTALLED)
				continue;
		}

		/* Conflict rule: (!A | !Bx) */
		rule = pkg_solve_rule_new(PKG_RULE_EXPLICIT_CONFLICT);
		if (rule == NULL)
			return (EPKG_FATAL);
		/* !A */
		it = pkg_solve_item_new(var);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = -1;
		RULE_ITEM_PREPEND(rule, it);
		/* !Bx */
		it = pkg_solve_item_new(curvar);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = -1;
		RULE_ITEM_PREPEND(rule, it);

		LL_PREPEND(problem->rules, rule);
		problem->rules_count ++;
	}

	return (EPKG_OK);
}

static int
pkg_solve_add_require_rule(struct pkg_solve_problem *problem,
		struct pkg_solve_variable *var,
		struct pkg_shlib *shlib)
{
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it = NULL;
	struct pkg_job_provide *pr, *prhead;
	int cnt;

	HASH_FIND_STR(problem->j->universe->provides, shlib->name, prhead);
	if (prhead != NULL) {
		/* Require rule !A | P1 | P2 | P3 ... */
		rule = pkg_solve_rule_new(PKG_RULE_REQUIRE);
		if (rule == NULL)
			return (EPKG_FATAL);
		/* !A */
		it = pkg_solve_item_new(var);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = -1;
		RULE_ITEM_PREPEND(rule, it);
		/* B1 | B2 | ... */
		cnt = 1;
		LL_FOREACH(prhead, pr) {
			if (pkg_solve_handle_provide(problem, pr, rule, &cnt) != EPKG_OK)
				return (EPKG_FATAL);
		}

		if (cnt > 1) {
			LL_PREPEND(problem->rules, rule);
			problem->rules_count ++;
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
		pkg_debug(1, "solver: cannot find provide for required shlib %s",
			shlib->name);
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
		if (it == NULL)
			return (EPKG_FATAL);

		/* All request variables are top level */
		curvar->top_level = true;
		curvar->to_install = inverse > 0;
		it->inverse = inverse;
		RULE_ITEM_PREPEND(rule, it);
		cnt ++;
	}

	if (cnt > 1 && var->unit->hh.keylen != 0) {
		LL_PREPEND(problem->rules, rule);
		problem->rules_count ++;
		/* Also need to add pairs of conflicts */
		LL_FOREACH(req->item, item) {
			curvar = pkg_solve_find_var_in_chain(var, item->unit);
			assert(curvar != NULL);
			if (item->next) {
				LL_FOREACH(item->next, confitem) {
					confvar = pkg_solve_find_var_in_chain(var, confitem->unit);
					assert(confvar != NULL && confvar != curvar && confvar != var);
					/* Conflict rule: (!A | !Bx) */
					rule = pkg_solve_rule_new(PKG_RULE_REQUEST_CONFLICT);
					if (rule == NULL)
						return (EPKG_FATAL);
					/* !A */
					it = pkg_solve_item_new(curvar);
					if (it == NULL)
						return (EPKG_FATAL);

					it->inverse = -1;
					RULE_ITEM_PREPEND(rule, it);
					/* !Bx */
					it = pkg_solve_item_new(confvar);
					if (it == NULL)
						return (EPKG_FATAL);

					it->inverse = -1;
					RULE_ITEM_PREPEND(rule, it);

					LL_PREPEND(problem->rules, rule);
					problem->rules_count ++;
				}
			}
		}
	}
	else {
		/* No need to add unary rules as we added the assumption already */
		pkg_solve_rule_free(rule);
	}

	var->top_level = true;
	var->to_install = inverse > 0;
	problem->rules_count ++;

	return (EPKG_OK);
}

static int
pkg_solve_add_chain_rule(struct pkg_solve_problem *problem,
	struct pkg_solve_variable *var)
{
	struct pkg_solve_variable *curvar;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it = NULL;

	LL_FOREACH(var->next, curvar) {
		/* Conflict rule: (!Ax | !Ay) */
		rule = pkg_solve_rule_new(PKG_RULE_UPGRADE_CONFLICT);
		if (rule == NULL)
			return (EPKG_FATAL);
		/* !Ax */
		it = pkg_solve_item_new(var);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = -1;
		RULE_ITEM_PREPEND(rule, it);
		/* !Ay */
		it = pkg_solve_item_new(curvar);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = -1;
		RULE_ITEM_PREPEND(rule, it);

		LL_PREPEND(problem->rules, rule);
		problem->rules_count ++;
	}

	return (EPKG_OK);
}

static int
pkg_solve_process_universe_variable(struct pkg_solve_problem *problem,
		struct pkg_solve_variable *var)
{
	struct pkg_dep *dep, *dtmp;
	struct pkg_conflict *conflict, *ctmp;
	struct pkg *pkg;
	struct pkg_solve_variable *cur_var;
	struct pkg_shlib *shlib = NULL;
	struct pkg_jobs *j = problem->j;
	struct pkg_job_request *jreq;
	bool chain_added = false;

	LL_FOREACH(var, cur_var) {
		pkg = cur_var->unit->pkg;

		/* Depends */
		HASH_ITER(hh, pkg->deps, dep, dtmp) {
			if (pkg_solve_add_depend_rule(problem, cur_var, dep) != EPKG_OK)
				continue;
		}

		/* Conflicts */
		HASH_ITER(hh, pkg->conflicts, conflict, ctmp) {
			if (pkg_solve_add_conflict_rule(problem, pkg, cur_var, conflict) !=
							EPKG_OK)
				continue;
		}

		/* Shlibs */
		shlib = NULL;
		if (pkg->type != PKG_INSTALLED) {
			while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
				if (pkg_solve_add_require_rule(problem, cur_var, shlib) != EPKG_OK)
					continue;
			}
		}

		/* Request */
		if (!cur_var->top_level) {
			HASH_FIND_STR(j->request_add, cur_var->uid, jreq);
			if (jreq != NULL)
				pkg_solve_add_request_rule(problem, cur_var, jreq, 1);
			HASH_FIND_STR(j->request_delete, cur_var->uid, jreq);
			if (jreq != NULL)
				pkg_solve_add_request_rule(problem, cur_var, jreq, -1);
		}

		/*
		 * If this var chain contains mutually conflicting vars
		 * we need to register conflicts with all following
		 * vars
		 */
		if (!chain_added && cur_var->next != NULL) {
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

	problem = calloc(1, sizeof(struct pkg_solve_problem));

	if (problem == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_problem");
		return (NULL);
	}

	problem->j = j;
	problem->nvars = j->universe->nitems;
	problem->variables = calloc(problem->nvars, sizeof(struct pkg_solve_variable));
	problem->sat = picosat_init();

	if (problem->sat == NULL) {
		pkg_emit_errno("picosat_init", "pkg_solve_sat_problem");
		return (NULL);
	}

	if (problem->variables == NULL) {
		pkg_emit_errno("calloc", "variables");
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
			    var->uid);
			goto err;
		}
		if (pkg_solve_process_universe_variable(problem, var) != EPKG_OK)
			goto err;
	}

	if (problem->rules_count == 0) {
		pkg_debug(1, "problem has no requests");
		return (problem);
	}

	return (problem);

err:
	return (NULL);
}

int
pkg_solve_sat_problem(struct pkg_solve_problem *problem)
{
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *item;
	int res;
	size_t i;

	LL_FOREACH(problem->rules, rule) {
		LL_FOREACH(rule->items, item) {
			picosat_add(problem->sat, item->var->order * item->inverse);
		}
		picosat_add(problem->sat, 0);
		pkg_debug_print_rule(rule);
	}
	/* Set initial guess */
	for (i = 0; i < problem->nvars; i ++)
	{
		struct pkg_solve_variable *var = &problem->variables[i];
		bool is_installed = var->unit->pkg->type == PKG_INSTALLED;

		if (var->top_level)
			continue;

		if (is_installed) {
			picosat_set_default_phase_lit(problem->sat, i + 1, 1);
			picosat_set_more_important_lit(problem->sat, i + 1);
		}
		else {
			picosat_set_default_phase_lit(problem->sat, i + 1, -1);
			picosat_set_less_important_lit(problem->sat, i + 1);
		}
	}

	res = picosat_sat(problem->sat, -1);

	if (res != PICOSAT_SATISFIABLE) {
		const int *failed = picosat_failed_assumptions(problem->sat);
		struct sbuf *sb = sbuf_new_auto();

		pkg_emit_error("Cannot solve problem using SAT solver:");

		while (*failed) {
			struct pkg_solve_variable *var = &problem->variables[*failed - 1];

			LL_FOREACH(problem->rules, rule) {
				LL_FOREACH(rule->items, item) {
					if (item->var == var) {
						pkg_print_rule_sbuf(rule, sb);
						sbuf_putc(sb, '\n');
					}
					break;
				}
			}

			sbuf_printf(sb, "cannot %s package %s, remove it from request? [Y/n]: ",
				var->to_install ? "install" : "remove", var->uid);
			sbuf_finish(sb);

			if (pkg_emit_query_yesno(true, sbuf_data(sb))) {
				struct pkg_job_request *req;
				/* Remove this assumption and the corresponding request */
				if (var->to_install)
					HASH_FIND_PTR(problem->j->request_add, &var->unit, req);
				else
					HASH_FIND_PTR(problem->j->request_delete, &var->unit, req);
				if (req == NULL) {
					pkg_emit_error("cannot find %s in the request", var->uid);
					return (EPKG_FATAL);
				}

				if (var->to_install)
					HASH_DEL(problem->j->request_add, req);
				else
					HASH_DEL(problem->j->request_delete, req);
				sbuf_reset(sb);
			}
			else {
				sbuf_free(sb);
				return (EPKG_FATAL);
			}

			failed ++;
		}

		sbuf_free(sb);

		return (EPKG_AGAIN);
	}

	/* Assign vars */
	for (i = 0; i < problem->nvars; i ++) {
		int val = picosat_deref(problem->sat, i + 1);
		struct pkg_solve_variable *var = &problem->variables[i];

		if (val > 0)
			var->to_install = true;
		else
			var->to_install = false;

		pkg_debug(2, "decided %s %s-%s(%d) to %s",
			var->unit->pkg->type == PKG_INSTALLED ? "local" : "remote",
			var->uid, var->digest,
			var->priority,
			var->to_install ? "install" : "delete");
	}

	return (EPKG_OK);
}

struct pkg_solve_ordered_variable {
	struct pkg_solve_variable *var;
	int order;
	UT_hash_handle hh;
};

int
pkg_solve_dimacs_export(struct pkg_solve_problem *problem, FILE *f)
{
	struct pkg_solve_ordered_variable *ordered_variables = NULL, *nord;
	struct pkg_solve_variable *var;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it;
	int cur_ord = 1;

	/* Order variables */
	var = NULL;
	while ((var = PKG_SOLVE_VAR_NEXT(problem->variables, var))) {
		nord = calloc(1, sizeof(struct pkg_solve_ordered_variable));
		nord->order = cur_ord ++;
		nord->var = var;
		HASH_ADD_PTR(ordered_variables, var, nord);
	}

	fprintf(f, "p cnf %d %d\n", (int)problem->nvars, problem->rules_count);

	LL_FOREACH(problem->rules, rule) {
		LL_FOREACH(rule->items, it) {
			HASH_FIND_PTR(ordered_variables, &it->var, nord);
			if (nord != NULL) {
				fprintf(f, "%s%d ", (it->inverse ? "-" : ""), nord->order);
			}
		}
		fprintf(f, "0\n");
	}

	HASH_FREE(ordered_variables, free);

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
		if (cur_var->to_install && cur_var->unit->pkg->type != PKG_INSTALLED) {
			add_var = cur_var;
			seen_add ++;
		}
		else if (!cur_var->to_install && cur_var->unit->pkg->type == PKG_INSTALLED) {
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
			res = calloc(1, sizeof(struct pkg_solved));
			if (res == NULL) {
				pkg_emit_errno("calloc", "pkg_solved");
				return;
			}
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
			if (!cur_var->to_install && cur_var->unit->pkg->type == PKG_INSTALLED) {
				/* Skip already added items */
				if (seen_add > 0 && cur_var == del_var)
					continue;

				res = calloc(1, sizeof(struct pkg_solved));
				if (res == NULL) {
					pkg_emit_errno("calloc", "pkg_solved");
					return;
				}
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

int
pkg_solve_parse_sat_output(FILE *f, struct pkg_solve_problem *problem, struct pkg_jobs *j)
{
	struct pkg_solve_ordered_variable *ordered_variables = NULL, *nord;
	struct pkg_solve_variable *var;
	int cur_ord = 1, ret = EPKG_OK;
	char *line = NULL, *var_str, *begin;
	size_t linecap = 0;
	ssize_t linelen;
	bool got_sat = false, done = false;

	/* Order variables */
	var = NULL;
	while ((var = PKG_SOLVE_VAR_NEXT(problem->variables, var))) {
		nord = calloc(1, sizeof(struct pkg_solve_ordered_variable));
		nord->order = cur_ord ++;
		nord->var = var;
		HASH_ADD_INT(ordered_variables, order, nord);
	}

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
				cur_ord = 0;
				cur_ord = abs((int)strtol(var_str, NULL, 10));
				if (cur_ord == 0) {
					done = true;
					break;
				}

				HASH_FIND_INT(ordered_variables, &cur_ord, nord);
				if (nord != NULL)
					nord->var->to_install = (*var_str != '-');
			} while (begin != NULL);
		}
		else if (strncmp(line, "v ", 2) == 0) {
			begin = line + 2;
			do {
				var_str = strsep(&begin, " \t");
				/* Skip unexpected lines */
				if (var_str == NULL || (!isdigit(*var_str) && *var_str != '-'))
					continue;
				cur_ord = 0;
				cur_ord = abs((int)strtol(var_str, NULL, 10));
				if (cur_ord == 0) {
					done = true;
					break;
				}

				HASH_FIND_INT(ordered_variables, &cur_ord, nord);
				if (nord != NULL)
					nord->var->to_install = (*var_str != '-');
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

	HASH_FREE(ordered_variables, free);
	free(line);

	return (ret);
}
