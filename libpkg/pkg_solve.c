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
#define _WITH_GETLINE
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

struct pkg_solve_variable {
	struct pkg *pkg;
	bool to_install;
	int priority;
	const char *digest;
	const char *origin;
	bool resolved;
	UT_hash_handle hd;
	UT_hash_handle ho;
	struct pkg_solve_variable *next;
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
	unsigned int rules_count;
	struct pkg_solve_rule *rules;
	struct pkg_solve_variable *variables_by_origin;
	struct pkg_solve_variable *variables_by_digest;
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
	int resolved = 0, total = 0, solved_vars;
	bool ret;

	do {
		solved_vars = 0;
		LL_FOREACH(rules, cur) {

			total = resolved = 0;
			LL_FOREACH(cur->items, it) {
				if (it->var->resolved && !PKG_SOLVE_CHECK_ITEM(it))
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
					solved_vars ++;
					pkg_debug(2, "propagate %s(%d) to %d",
							unresolved->var->origin, unresolved->var->priority, unresolved->var->to_install);
				}
				/* Now check for a conflict */
				ret = false;
				LL_FOREACH(cur->items, it) {
					if (PKG_SOLVE_CHECK_ITEM(it))
						ret = true;
				}
				/* A conflict found */
				if (!ret)
					return (false);
			}
		}
	} while (solved_vars > 0);

	return (true);
}


/**
 * Propagate pure clauses
 */
static bool
pkg_solve_propagate_pure(struct pkg_solve_rule *rules)
{
	struct pkg_solve_rule *cur;
	struct pkg_solve_item *it;

	LL_FOREACH(rules, cur) {
		it = cur->items;
		/* Unary rules */
		if (!it->var->resolved && it->next == NULL) {
			it->var->to_install = !it->inverse;
			it->var->resolved = true;
		}
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
			if (it->var->resolved && !next->var->resolved) {
				if (!PKG_SOLVE_CHECK_ITEM(it))
					return (false);
			}
			else if (!it->var->resolved && next->var->resolved) {
				if (!PKG_SOLVE_CHECK_ITEM(next))
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
					if (pkg_solve_check_conflicts(rules)) {
						pkg_debug(2, "assume %s(%d) to %d",
								it->var->origin, it->var->priority, it->var->to_install);
						it->var->resolved = true;
					}
				}
				else {
					it->var->to_install = false;
					if (pkg_solve_check_conflicts(rules)) {
						pkg_debug(2, "assume %s(%d) to %d",
								it->var->origin, it->var->priority, it->var->to_install);
						it->var->resolved = true;
					}
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
	pkg_solve_propagate_pure(problem->rules);

	while (!pkg_solve_check_rules(problem->rules)) {
		/* TODO:
		 * 1) assign a free variable
		 * 2) check for contradictions
		 * 3) analyse and learn
		 * 4) undo an assignment
		 */
		if (!pkg_solve_propagate_units(problem->rules)) {
			pkg_emit_error("unimplemented: cannot solve SAT problem as units propagation has fallen");
			return (false);
		}
		pkg_solve_propagate_default(problem->rules);
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
pkg_solve_variable_new(struct pkg *pkg, int priority)
{
	struct pkg_solve_variable *result;
	const char *digest, *origin;

	result = calloc(1, sizeof(struct pkg_solve_variable));

	if(result == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_variable");
		return (NULL);
	}

	result->pkg = pkg;
	result->priority = priority;
	pkg_get(pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest);
	/* XXX: Is it safe to save a ptr here ? */
	result->digest = digest;
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
	HASH_ITER(hd, problem->variables_by_digest, v, vtmp) {
		HASH_DELETE(hd, problem->variables_by_digest, v);
		free(v);
	}
}

static int
pkg_solve_add_universe_variable(struct pkg_jobs *j,
		struct pkg_solve_problem *problem, const char *origin, struct pkg_solve_variable **var)
{
	struct pkg_job_universe_item *unit;
	struct pkg_solve_variable *nvar, *tvar;

	HASH_FIND_STR(j->universe, __DECONST(char *, origin), unit);
	/* If there is no package in universe, refuse continue */
	if (unit == NULL) {
		pkg_emit_error("package %s is not found in universe", origin);
		return (EPKG_FATAL);
	}
	/* Need to add a variable */
	nvar = pkg_solve_variable_new(unit->pkg, unit->priority);
	if (nvar == NULL)
		return (EPKG_FATAL);
	HASH_ADD_KEYPTR(ho, problem->variables_by_origin, nvar->origin, strlen(nvar->origin), nvar);
	HASH_ADD_KEYPTR(hd, problem->variables_by_digest, nvar->digest, strlen(nvar->digest), nvar);
	unit = unit->next;
	tvar = nvar;
	while (unit != NULL) {
		/* Add all alternatives as independent variables */
		tvar->next = pkg_solve_variable_new(unit->pkg, unit->priority);
		tvar = tvar->next;
		if (tvar == NULL)
			return (EPKG_FATAL);
		HASH_ADD_KEYPTR(hd, problem->variables_by_digest, tvar->digest, strlen(tvar->digest), tvar);
		unit = unit->next;
	}

	*var = nvar;

	return (EPKG_OK);
}

static int
pkg_solve_add_pkg_rule(struct pkg_jobs *j, struct pkg_solve_problem *problem,
		struct pkg_solve_variable *pvar, bool conflicting)
{
	struct pkg_dep *dep, *dtmp;
	struct pkg_conflict *conflict, *ctmp;
	struct pkg *pkg;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it = NULL;
	struct pkg_solve_variable *var, *tvar, *cur_var;

	const char *origin;

	/* Go through all deps in all variables*/
	LL_FOREACH(pvar, cur_var) {
		pkg = cur_var->pkg;
		HASH_ITER(hh, pkg->deps, dep, dtmp) {
			rule = NULL;
			it = NULL;
			var = NULL;

			origin = pkg_dep_get(dep, PKG_DEP_ORIGIN);
			HASH_FIND(ho, problem->variables_by_origin,
					__DECONST(char *, origin), strlen(origin), var);
			if (var == NULL) {
				if (pkg_solve_add_universe_variable(j, problem, origin, &var) != EPKG_OK)
					goto err;
			}
			/* Dependency rule: (!A | B) */
			rule = pkg_solve_rule_new();
			if (rule == NULL)
				goto err;
			/* !A */
			it = pkg_solve_item_new(cur_var);
			if (it == NULL)
				goto err;

			it->inverse = true;
			LL_PREPEND(rule->items, it);
			/* B1 | B2 | ... */
			LL_FOREACH(var, tvar) {
				it = pkg_solve_item_new(tvar);
				if (it == NULL)
					goto err;

				it->inverse = false;
				LL_PREPEND(rule->items, it);
			}

			LL_PREPEND(problem->rules, rule);
			problem->rules_count ++;
		}

		/* Go through all conflicts */
		HASH_ITER(hh, pkg->conflicts, conflict, ctmp) {
			rule = NULL;
			it = NULL;
			var = NULL;

			origin = pkg_conflict_origin(conflict);
			HASH_FIND(ho, problem->variables_by_origin,
					__DECONST(char *, origin), strlen(origin), var);
			if (var == NULL) {
				if (pkg_solve_add_universe_variable(j, problem, origin, &var) != EPKG_OK)
					goto err;
			}
			/* Add conflict rule from each of the alternative */
			LL_FOREACH(var, tvar) {
				/* Conflict rule: (!A | !Bx) */
				rule = pkg_solve_rule_new();
				if (rule == NULL)
					goto err;
				/* !A */
				it = pkg_solve_item_new(cur_var);
				if (it == NULL)
					goto err;

				it->inverse = true;
				LL_PREPEND(rule->items, it);
				/* !Bx */
				it = pkg_solve_item_new(tvar);
				if (it == NULL)
					goto err;

				it->inverse = true;
				LL_PREPEND(rule->items, it);

				LL_PREPEND(problem->rules, rule);
				problem->rules_count ++;
			}
		}

		if (conflicting) {
			/*
			 * If this var chain contains mutually conflicting vars
			 * we need to register conflicts with all following
			 * vars
			 */
			var = cur_var->next;
			if (var != NULL) {
				LL_FOREACH(var, tvar) {
					/* Conflict rule: (!Ax | !Ay) */
					rule = pkg_solve_rule_new();
					if (rule == NULL)
						goto err;
					/* !Ax */
					it = pkg_solve_item_new(cur_var);
					if (it == NULL)
						goto err;

					it->inverse = true;
					LL_PREPEND(rule->items, it);
					/* !Ay */
					it = pkg_solve_item_new(tvar);
					if (it == NULL)
						goto err;

					it->inverse = true;
					LL_PREPEND(rule->items, it);

					LL_PREPEND(problem->rules, rule);
					problem->rules_count ++;
				}
			}
		}
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
	struct pkg_job_universe_item *un, *utmp, *ucur;
	struct pkg_solve_variable *var, *tvar;
	const char *origin, *digest;

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

		var = pkg_solve_variable_new(jreq->pkg, jreq->priority);
		if (var == NULL)
			goto err;

		HASH_ADD_KEYPTR(hd, problem->variables_by_digest, var->digest, strlen(var->digest), var);
		HASH_ADD_KEYPTR(ho, problem->variables_by_origin, var->origin, strlen(var->origin), var);
		it = pkg_solve_item_new(var);
		if (it == NULL)
			goto err;

		rule = pkg_solve_rule_new();
		if (rule == NULL)
			goto err;

		/* Requests are unary rules */
		LL_PREPEND(rule->items, it);
		LL_PREPEND(problem->rules, rule);
		problem->rules_count ++;
	}
	HASH_ITER(hh, j->request_delete, jreq, jtmp) {
		rule = NULL;
		it = NULL;
		var = NULL;

		var = pkg_solve_variable_new(jreq->pkg, jreq->priority);
		if (var == NULL)
			goto err;

		HASH_ADD_KEYPTR(hd, problem->variables_by_digest, var->digest, strlen(var->digest), var);
		HASH_ADD_KEYPTR(ho, problem->variables_by_origin, var->origin, strlen(var->origin), var);
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
		problem->rules_count ++;
	}

	/* Parse universe */
	HASH_ITER(hh, j->universe, un, utmp) {
		rule = NULL;
		it = NULL;
		var = NULL;

		/* Add corresponding variables */
		LL_FOREACH(un, ucur) {
			pkg_get(ucur->pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest);
			HASH_FIND(hd, problem->variables_by_digest, digest, strlen(digest), var);
			if (var == NULL) {
				/* Add new variable */
				var = pkg_solve_variable_new(ucur->pkg, ucur->priority);
				if (var == NULL)
					goto err;
				HASH_ADD_KEYPTR(hd, problem->variables_by_digest,
						var->digest, strlen(var->digest), var);

				/* Check origin */
				HASH_FIND(ho, problem->variables_by_origin, origin, strlen(origin), tvar);
				if (tvar == NULL) {
					HASH_ADD_KEYPTR(ho, problem->variables_by_origin,
							var->origin, strlen(var->origin), var);
				}
				else {
					/* Insert a variable to a chain */
					tvar->next = var;
				}
			}
		}
		HASH_FIND(ho, problem->variables_by_origin, origin, strlen(origin), var);
		/* Now `var' contains a variables chain related to this origin */
		if (pkg_solve_add_pkg_rule(j, problem, var, true) == EPKG_FATAL)
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

struct pkg_solve_ordered_variable {
	struct pkg_solve_variable *var;
	int order;
	UT_hash_handle hh;
};

int
pkg_solve_dimacs_export(struct pkg_solve_problem *problem, FILE *f)
{
	struct pkg_solve_ordered_variable *ordered_variables = NULL, *nord;
	struct pkg_solve_variable *var, *vtmp;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it;
	int cur_ord = 1;

	/* Order variables */
	HASH_ITER(hd, problem->variables_by_digest, var, vtmp) {
		nord = calloc(1, sizeof(struct pkg_solve_ordered_variable));
		nord->order = cur_ord ++;
		nord->var = var;
		HASH_ADD_PTR(ordered_variables, var, nord);
	}

	fprintf(f, "p cnf %d %d\n", HASH_CNT(hd, problem->variables_by_digest),
			problem->rules_count);

	LL_FOREACH(problem->rules, rule) {
		LL_FOREACH(rule->items, it) {
			HASH_FIND_PTR(ordered_variables, &it->var, nord);
			if (nord != NULL) {
				fprintf(f, "%s%d ", (it->inverse ? "-" : ""), nord->order);
			}
		}
		fprintf(f, "0\n");
	}

	HASH_FREE(ordered_variables, pkg_solve_ordered_variable, free);

	return (EPKG_OK);
}

static void
pkg_solve_insert_res_job (struct pkg_solved **target, struct pkg_solve_variable *var)
{
	struct pkg_solved *res;

	res = calloc(1, sizeof(struct pkg_solved));
	if (res == NULL) {
		pkg_emit_errno("calloc", "pkg_solved");
		return;
	}
	res->priority = var->priority;
	res->pkg = var->pkg;
	DL_APPEND(*target, res);
}

int
pkg_solve_sat_to_jobs(struct pkg_solve_problem *problem, struct pkg_jobs *j)
{
	struct pkg_solve_variable *var, *vtmp;
	const char *origin;

	HASH_ITER(hd, problem->variables_by_digest, var, vtmp) {
		if (!var->resolved)
			return (EPKG_FATAL);

		pkg_get(var->pkg, PKG_ORIGIN, &origin);
		if (var->to_install && var->pkg->type != PKG_INSTALLED) {
			pkg_solve_insert_res_job(&j->jobs_add, var);
			j->count ++;
		}
		else if (!var->to_install && var->pkg->type == PKG_INSTALLED) {
			pkg_solve_insert_res_job(&j->jobs_delete, var);
			j->count ++;
		}
	}

	return (EPKG_OK);
}

int
pkg_solve_parse_sat_output(FILE *f, struct pkg_solve_problem *problem, struct pkg_jobs *j)
{
	struct pkg_solve_ordered_variable *ordered_variables = NULL, *nord;
	struct pkg_solve_variable *var, *vtmp;
	int cur_ord = 1, ret = EPKG_OK;
	char *line = NULL, *var_str, *begin;
	size_t linecap = 0;
	ssize_t linelen;
	bool got_sat = false, done = false;

	/* Order variables */
	HASH_ITER(hd, problem->variables_by_digest, var, vtmp) {
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
				cur_ord = abs(strtol(var_str, NULL, 10));
				if (cur_ord == 0) {
					done = true;
					break;
				}

				HASH_FIND_INT(ordered_variables, &cur_ord, nord);
				if (nord != NULL) {
					nord->var->resolved = true;
					nord->var->to_install = (*var_str != '-');
				}
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
				cur_ord = abs(strtol(var_str, NULL, 10));
				if (cur_ord == 0) {
					done = true;
					break;
				}

				HASH_FIND_INT(ordered_variables, &cur_ord, nord);
				if (nord != NULL) {
					nord->var->resolved = true;
					nord->var->to_install = (*var_str != '-');
				}
			} while (begin != NULL);
		}
		else {
			/* Slightly ignore anything from solver */
			continue;
		}
	}

	if (done)
		ret = pkg_solve_sat_to_jobs(problem, j);
	else {
		pkg_emit_error("cannot parse sat solver output");
		ret = EPKG_FATAL;
	}

	HASH_FREE(ordered_variables, pkg_solve_ordered_variable, free);
	if (line != NULL)
		free(line);
	return (ret);
}
