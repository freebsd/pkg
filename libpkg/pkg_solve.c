/*-
 * Copyright (c) 2013 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <libutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

struct pkg_solve_variable {
	struct pkg *pkg;
	bool to_install;
	const char *origin;
	bool resolved;
	UT_hash_handle hh;
};

struct pkg_solve_item {
	struct pkg_solve_variable *var;
	bool inverse;
	struct pkg_solve_item *next;
};

struct pkg_solve_rule {
	struct pkg_solve_item *items;
	struct pkg_solve_rule *next;
};

struct pkg_solve_problem {
	struct pkg_solve_rule *rules;
	struct pkg_solve_variable *variables;
};

/*
 * Use XOR here to implement the following logic:
 * atom is true if it is installed and not inverted or
 * if it is not installed but inverted
 */
#define PKG_SOLVE_CHECK_ITEM(item)				\
	((item)->var->to_install ^ (item)->inverse)

/**
 * Check whether SAT rule is TRUE
 * @param rules list of rules
 * @return true or false
 */
static bool
pkg_solve_check_rules(struct pkg_solve_rule *rules)
{
	struct pkg_solve_rule *cur;
	struct pkg_solve_item *it;
	bool ret;

	LL_FOREACH(rules, cur) {

		ret = false;
		LL_FOREACH(cur->items, it) {
			if (it->var->resolved) {
				if (PKG_SOLVE_CHECK_ITEM(it))
					ret = true;
			}
		}
		if (!ret)
			return (false);

	}

	return (true);
}

/**
 * Propagate all units, must be called recursively
 * @param rules
 * @return true if there are units propagated
 */
static bool
pkg_solve_propagate_units(struct pkg_solve_rule *rules)
{
	struct pkg_solve_rule *cur;
	struct pkg_solve_item *it, *unresolved = NULL;
	int resolved = 0, total = 0;

	LL_FOREACH(rules, cur) {

		LL_FOREACH(cur->items, it) {
			if (it->var->resolved && PKG_SOLVE_CHECK_ITEM(it))
				resolved++;
			else
				unresolved = it;
			total ++;
		}
		/* It is a unit */
		if (total == resolved + 1 && unresolved != NULL) {
			if (!unresolved->var->resolved) {
				/* Propagate unit */
				unresolved->var->resolved = true;
				unresolved->var->to_install = !unresolved->inverse;
				return (true);
			}
		}
	}

	return (false);
}


/**
 * Propagate rules explicitly
 */
static bool
pkg_solve_propagate_explicit(struct pkg_solve_rule *rules)
{
	struct pkg_solve_rule *cur;
	struct pkg_solve_item *it;

	LL_FOREACH(rules, cur) {

		it = cur->items;
		/* Unary rules */
		if (!it->var->resolved && it->next == NULL) {
			it->var->to_install = !it->inverse;
			it->var->resolved = true;
			return (true);
		}

		return (pkg_solve_propagate_units(rules));
	}

	return (false);
}

static bool
pkg_solve_check_conflicts(struct pkg_solve_rule *rules)
{
	struct pkg_solve_rule *cur;
	struct pkg_solve_item *it, *next;

	/*
	 * Conflicts are presented as:
	 * (!A | !B), so check for them
	 */
	LL_FOREACH(rules, cur) {
		it = cur->items;
		next = it->next;
		if (next != NULL && next->next == NULL) {
			if (it->var->resolved ^ next->var->resolved) {
				if (!(PKG_SOLVE_CHECK_ITEM(it) && PKG_SOLVE_CHECK_ITEM(next)))
					return (false);
			}
		}
	}

	return (true);
}

/**
 * Use the default propagation policy:
 * - do not deinstall packages that are not in conflict
 * - do not install additional new packages
 *
 * This must be called after the explicit propagation
 */
static void
pkg_solve_propagate_default(struct pkg_solve_rule *rules)
{
	struct pkg_solve_rule *cur;
	struct pkg_solve_item *it;

	LL_FOREACH(rules, cur) {
		LL_FOREACH(cur->items, it) {
			if (!it->var->resolved) {
				if (it->var->pkg->type == PKG_INSTALLED) {
					it->var->to_install = true;
					if (pkg_solve_check_conflicts(rules))
						it->var->resolved = true;
				}
				else {
					it->var->to_install = false;
					if (pkg_solve_check_conflicts(rules))
						it->var->resolved = true;
				}
			}
		}
	}
}

/**
 * Try to solve sat problem
 * @param rules incoming rules to solve
 * @param nrules number of rules
 * @param nvars number of variables
 * @return
 */
bool
pkg_solve_sat_problem(struct pkg_solve_problem *problem)
{

	/* Initially propagate explicit rules */
	while (pkg_solve_propagate_explicit(problem->rules));

	/* Now try to assign default values */
	pkg_solve_propagate_default(problem->rules);

	while (!pkg_solve_check_rules(problem->rules)) {
		/* TODO:
		 * 1) assign a free variable
		 * 2) check for contradictions
		 * 3) analyse and learn
		 * 4) undo an assignment
		 */
	}

	return (true);
}

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

	return (result);
}

static struct pkg_solve_rule *
pkg_solve_rule_new(void)
{
	struct pkg_solve_rule *result;

	result = calloc(1, sizeof(struct pkg_solve_rule));

	if(result == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_rule");
		return (NULL);
	}

	return (result);
}

static struct pkg_solve_variable *
pkg_solve_variable_new(struct pkg *pkg)
{
	struct pkg_solve_variable *result;
	const char *origin;

	result = calloc(1, sizeof(struct pkg_solve_variable));

	if(result == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_variable");
		return (NULL);
	}

	result->pkg = pkg;
	pkg_get(pkg, PKG_ORIGIN, &origin);
	/* XXX: Is it safe to save a ptr here ? */
	result->origin = origin;

	return (result);
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
	HASH_ITER(hh, problem->variables, v, vtmp) {
		HASH_DEL(problem->variables, v);
		free(v);
	}
}

static int
pkg_solve_add_pkg_rule(struct pkg_jobs *j, struct pkg_solve_problem *problem,
		struct pkg_solve_variable *pvar)
{
	struct pkg_dep *dep, *dtmp;
	struct pkg_conflict *conflict, *ctmp;
	struct pkg *ptmp, *pkg = pvar->pkg;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it;
	struct pkg_solve_variable *var;
	const char *origin;

	/* Go through all deps */
	HASH_ITER(hh, pkg->deps, dep, dtmp) {
		rule = NULL;
		it = NULL;
		var = NULL;

		origin = pkg_dep_get(dep, PKG_DEP_ORIGIN);
		HASH_FIND_STR(problem->variables, __DECONST(char *, origin), var);
		if (var == NULL) {
			HASH_FIND_STR(j->universe, __DECONST(char *, origin), ptmp);
			/* If there is no package in universe, refuse continue */
			if (ptmp == NULL)
				return (EPKG_FATAL);
			/* Need to add a variable */
			var = pkg_solve_variable_new(ptmp);
			if (var == NULL)
				goto err;
			HASH_ADD_KEYPTR(hh, problem->variables, var->origin, strlen(var->origin), var);
		}
		/* Dependency rule: (!A | B) */
		rule = pkg_solve_rule_new();
		if (rule == NULL)
			goto err;
		/* !A */
		it = pkg_solve_item_new(pvar);
		if (it == NULL)
			goto err;

		it->inverse = true;
		LL_PREPEND(rule->items, it);
		/* B */
		it = pkg_solve_item_new(var);
		if (it == NULL)
			goto err;

		it->inverse = false;
		LL_PREPEND(rule->items, it);

		LL_PREPEND(problem->rules, rule);
	}

	/* Go through all conflicts */
	HASH_ITER(hh, pkg->conflicts, conflict, ctmp) {
		rule = NULL;
		it = NULL;
		var = NULL;

		origin = pkg_conflict_origin(conflict);
		HASH_FIND_STR(problem->variables, __DECONST(char *, origin), var);
		if (var == NULL) {
			HASH_FIND_STR(j->universe, __DECONST(char *, origin), ptmp);
			/* If there is no package in universe, refuse continue */
			if (ptmp == NULL)
				return (EPKG_FATAL);
			/* Need to add a variable */
			var = pkg_solve_variable_new(ptmp);
			if (var == NULL)
				goto err;
			HASH_ADD_KEYPTR(hh, problem->variables, var->origin, strlen(var->origin), var);
		}
		/* Conflict rule: (!A | !B) */
		rule = pkg_solve_rule_new();
		if (rule == NULL)
			goto err;
		/* !A */
		it = pkg_solve_item_new(pvar);
		if (it == NULL)
			goto err;

		it->inverse = true;
		LL_PREPEND(rule->items, it);
		/* !B */
		it = pkg_solve_item_new(var);
		if (it == NULL)
			goto err;

		it->inverse = true;
		LL_PREPEND(rule->items, it);

		LL_PREPEND(problem->rules, rule);
	}

	return (EPKG_OK);
err:
	if (it != NULL)
		free(it);
	if (var != NULL)
		free(var);
	if (rule != NULL)
		pkg_solve_rule_free(rule);
	return (EPKG_FATAL);
}

struct pkg_solve_problem *
pkg_solve_jobs_to_sat(struct pkg_jobs *j)
{
	struct pkg_solve_problem *problem;
	struct pkg_job_request *jreq, *jtmp;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it;
	struct pkg *pkg, *ptmp;
	struct pkg_solve_variable *var;
	const char *origin;

	problem = calloc(1, sizeof(struct pkg_solve_problem));

	if (problem == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_problem");
		return (NULL);
	}

	/* Add requests */
	HASH_ITER(hh, j->request_add, jreq, jtmp) {
		rule = NULL;
		it = NULL;
		var = NULL;

		var = pkg_solve_variable_new(jreq->pkg);
		if (var == NULL)
			goto err;

		HASH_ADD_KEYPTR(hh, problem->variables, var->origin, strlen(var->origin), var);
		it = pkg_solve_item_new(var);
		if (it == NULL)
			goto err;

		rule = pkg_solve_rule_new();
		if (rule == NULL)
			goto err;

		/* Requests are unary rules */
		LL_PREPEND(rule->items, it);
		LL_PREPEND(problem->rules, rule);
	}
	HASH_ITER(hh, j->request_delete, jreq, jtmp) {
		rule = NULL;
		it = NULL;
		var = NULL;

		var = pkg_solve_variable_new(jreq->pkg);
		if (var == NULL)
			goto err;

		HASH_ADD_KEYPTR(hh, problem->variables, var->origin, strlen(var->origin), var);
		it = pkg_solve_item_new(var);
		if (it == NULL)
			goto err;

		it->inverse = true;
		rule = pkg_solve_rule_new();
		if (rule == NULL)
			goto err;

		/* Requests are unary rules */
		LL_PREPEND(rule->items, it);
		LL_PREPEND(problem->rules, rule);
	}

	/* Parse universe */
	HASH_ITER(hh, j->universe, pkg, ptmp) {
		rule = NULL;
		it = NULL;
		var = NULL;

		pkg_get(pkg, PKG_ORIGIN, &origin);
		HASH_FIND_STR(problem->variables, origin, var);
		if (var == NULL) {
			/* Add new variable */
			var = pkg_solve_variable_new(pkg);
			if (var == NULL)
				goto err;
			HASH_ADD_KEYPTR(hh, problem->variables, var->origin, strlen(var->origin), var);
		}
		if (pkg_solve_add_pkg_rule(j, problem, var) == EPKG_FATAL)
			goto err;
	}

	return (problem);
err:
	if (it != NULL)
		free(it);
	if (var != NULL)
		free(var);
	if (rule != NULL)
		pkg_solve_rule_free(rule);
	return (NULL);
}
