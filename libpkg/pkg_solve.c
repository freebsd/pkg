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

struct pkg_solve_item;

struct pkg_solve_variable {
	struct pkg_job_universe_item *unit;
	bool to_install;
	bool guess;
	int priority;
	const char *digest;
	const char *origin;
	bool resolved;
	struct _pkg_solve_var_rule {
		struct pkg_solve_item *rule;
		struct _pkg_solve_var_rule *next;
	} *rules;
	int nrules;
	UT_hash_handle hd;
	UT_hash_handle ho;
	struct pkg_solve_variable *next, *prev;
};

struct pkg_solve_item {
	int nitems;
	int nresolved;
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
pkg_solve_check_rules(struct pkg_solve_problem *problem)
{
	struct pkg_solve_variable *var, *tvar;

	HASH_ITER(hd, problem->variables_by_digest, var, tvar) {
		if (!var->resolved) {
			pkg_debug(2, "solver: var %s-%s is not still resolved",
					var->origin, var->digest);
			return false;
		}
	}

	return (true);
}

/**
 * Updates rules related to a single variable
 * @param var
 */
static void
pkg_solve_update_var_resolved (struct pkg_solve_variable *var)
{
	struct _pkg_solve_var_rule *rul;

	LL_FOREACH(var->rules, rul) {
		rul->rule->nresolved ++;
	}
}

/**
 * Propagate all units, must be called recursively
 * @param rules
 * @return true if there are units propagated
 */
static bool
pkg_solve_propagate_units(struct pkg_solve_problem *problem, int *propagated)
{
	struct pkg_solve_item *it, *unresolved = NULL;
	int solved_vars;
	struct _pkg_solve_var_rule *rul;
	struct pkg_solve_variable *var, *tvar;
	bool ret;

	do {
		solved_vars = 0;
		HASH_ITER(hd, problem->variables_by_digest, var, tvar) {
check_again:
			/* Check for direct conflicts */
			LL_FOREACH(var->rules, rul) {
				unresolved = rul->rule;
				if (unresolved->nresolved == unresolved->nitems) {
					/* Check for direct conflict */
					ret = false;
					LL_FOREACH(unresolved, it) {
						if (it->var->resolved) {
							if (PKG_SOLVE_CHECK_ITEM(it))
								ret = true;
						}
					}
					if (!ret) {
						struct sbuf *err_msg = sbuf_new_auto();
						sbuf_printf(err_msg, "cannot resolve conflict between ");
						LL_FOREACH(unresolved, it) {
							sbuf_printf(err_msg, "%s %s(want %s), ",
									it->var->unit->pkg->type == PKG_INSTALLED ? "local" : "remote",
											it->var->origin,
											it->var->to_install ? "install" : "remove");
						}
						sbuf_finish(err_msg);
						pkg_emit_error("%splease resolve it manually", sbuf_data(err_msg));
						sbuf_delete(err_msg);
						return (false);
					}
				}
			}
			LL_FOREACH(var->rules, rul) {
				unresolved = rul->rule;
				if (unresolved->nresolved == unresolved->nitems - 1) {
					/* Check for unit */
					ret = false;
					LL_FOREACH(unresolved, it) {
						if (it->var->resolved) {
							if (PKG_SOLVE_CHECK_ITEM(it))
								ret = true;
						}
					}
					if (!ret) {
						/* This is a unit */
						LL_FOREACH(unresolved, it) {
							if (!it->var->resolved) {
								it->var->to_install = (!it->inverse);
								it->var->resolved = true;
								pkg_solve_update_var_resolved(it->var);
								pkg_debug(2, "propagate %s-%s(%d) to %s",
										it->var->origin, it->var->digest,
										it->var->priority,
										it->var->to_install ? "install" : "delete");
								break;
							}
						}
						solved_vars ++;
						(*propagated) ++;
						/* We want to try propagating all clauses for a single variable */
						goto check_again;
					}
				}
			}
		}
	} while (solved_vars > 0);

	return (true);
}


/**
 * Propagate pure clauses
 */
static bool
pkg_solve_propagate_pure(struct pkg_solve_problem *problem)
{
	struct pkg_solve_variable *var, *tvar;
	struct _pkg_solve_var_rule *rul;
	struct pkg_solve_item *it;

	HASH_ITER(hd, problem->variables_by_digest, var, tvar) {
		if (var->nrules == 0) {
			/* This variable is independent and should not change its state */
			var->to_install = (var->unit->pkg->type == PKG_INSTALLED);
			var->resolved = true;
			pkg_debug(2, "leave %s-%s(%d) to %s",
					var->origin, var->digest,
					var->priority, var->to_install ? "install" : "delete");
		}
		else {
			LL_FOREACH(var->rules, rul) {
				it = rul->rule;
				if (it->nitems == 1 && it->nresolved == 0) {
					it->var->to_install = (!it->inverse);
					it->var->resolved = true;
					pkg_debug(2, "requested %s-%s(%d) to %s",
							it->var->origin, it->var->digest,
							it->var->priority, it->var->to_install ? "install" : "delete");
					pkg_solve_update_var_resolved(it->var);
				}
			}
		}
	}

	return (true);
}

/**
 * Test intermediate SAT guess
 * @param problem
 * @return true if guess is accepted
 */
bool
pkg_solve_test_guess(struct pkg_solve_problem *problem)
{
	bool test = false;
	struct pkg_solve_variable *var, *tvar;
	struct _pkg_solve_var_rule *rul;
	struct pkg_solve_item *it, *cur;

	HASH_ITER(hd, problem->variables_by_digest, var, tvar) {
		LL_FOREACH(var->rules, rul) {
			it = rul->rule;
			if (it->nitems != it->nresolved) {
				/* Check guess */
				test = false;
				LL_FOREACH(it, cur) {
					if (cur->var->resolved)
						test |= cur->var->to_install ^ cur->inverse;
					else
						test |= cur->var->guess ^ cur->inverse;
				}
				if (!test)
					return (false);
			}
		}
	}

	return (true);
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
	int propagated, iters = 0;
	bool guessed = false;
	struct pkg_solve_variable *var, *tvar;

	/* Obvious case */
	if (problem->rules_count == 0)
		return (true);

	/* Initially propagate explicit rules */
	pkg_solve_propagate_pure(problem);
	if (!pkg_solve_propagate_units(problem, &propagated)) {
		pkg_emit_error("SAT: conflicting request, cannot solve");
		return (false);
	}

	/* Set initial guess */
	HASH_ITER(hd, problem->variables_by_digest, var, tvar) {
		if (!var->resolved) {
			/* Guess true for installed packages and false otherwise */
			var->guess = (var->unit->pkg->type == PKG_INSTALLED) ? true : false;
		}
	}

	while (!guessed) {
		HASH_ITER(hd, problem->variables_by_digest, var, tvar) {
			if (pkg_solve_test_guess(problem)) {
				guessed = true;
				break;
			}
			else {
				var->guess = !var->guess;
				if (pkg_solve_test_guess(problem)) {
					guessed = true;
					break;
				}
			}
		}
		iters ++;
	}

	pkg_debug(1, "solved SAT problem in %d guesses", iters);

	HASH_ITER(hd, problem->variables_by_digest, var, tvar) {
		if (!var->resolved) {
			var->to_install = var->guess;
			var->resolved = true;
		}
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
	result->inverse = false;

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
pkg_solve_variable_new(struct pkg_job_universe_item *item)
{
	struct pkg_solve_variable *result;
	const char *digest, *origin;

	result = calloc(1, sizeof(struct pkg_solve_variable));

	if(result == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_variable");
		return (NULL);
	}

	result->unit = item;
	pkg_get(item->pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest);
	/* XXX: Is it safe to save a ptr here ? */
	result->digest = digest;
	result->origin = origin;
	result->prev = result;

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
	struct _pkg_solve_var_rule *vrule, *vrtmp;

	LL_FOREACH_SAFE(problem->rules, r, rtmp) {
		pkg_solve_rule_free(r);
	}
	HASH_ITER(hd, problem->variables_by_digest, v, vtmp) {
		HASH_DELETE(hd, problem->variables_by_digest, v);
		LL_FOREACH_SAFE(v->rules, vrule, vrtmp) {
			free(vrule);
		}
		free(v);
	}
}

static int
pkg_solve_add_universe_variable(struct pkg_jobs *j,
		struct pkg_solve_problem *problem, const char *origin, struct pkg_solve_variable **var)
{
	struct pkg_job_universe_item *unit;
	struct pkg_solve_variable *nvar, *tvar = NULL, *found;
	const char *digest;

	HASH_FIND_STR(j->universe, origin, unit);
	/* If there is no package in universe, refuse continue */
	if (unit == NULL) {
		pkg_debug(2, "package %s is not found in universe", origin);
		return (EPKG_FATAL);
	}
	/* Need to add a variable */
	nvar = pkg_solve_variable_new(unit);
	if (nvar == NULL)
		return (EPKG_FATAL);

	HASH_ADD_KEYPTR(hd, problem->variables_by_digest, nvar->digest, strlen(nvar->digest), nvar);
	HASH_ADD_KEYPTR(ho, problem->variables_by_origin, nvar->origin, strlen(nvar->origin), nvar);
	pkg_debug(4, "solver: add variable from universe with origin %s", nvar->origin);

	unit = unit->next;
	while (unit != NULL) {
		pkg_get(unit->pkg, PKG_DIGEST, &digest);
		HASH_FIND(hd, problem->variables_by_digest, digest, strlen(digest), found);
		if (found == NULL) {
			/* Add all alternatives as independent variables */
			tvar = pkg_solve_variable_new(unit);
			if (tvar == NULL)
				return (EPKG_FATAL);
			DL_APPEND(nvar, tvar);
			HASH_ADD_KEYPTR(hd, problem->variables_by_digest, tvar->digest,
					strlen(tvar->digest), tvar);
			pkg_debug (4, "solver: add another variable with origin %s and digest %s",
					tvar->origin, tvar->digest);
		}
		unit = unit->next;
	}

	*var = nvar;

	return (EPKG_OK);
}

static int
pkg_solve_add_var_rules (struct pkg_solve_variable *var,
		struct pkg_solve_item *rule, int nrules, bool iterate_vars,
		const char *desc)
{
	struct _pkg_solve_var_rule *head = NULL;
	struct pkg_solve_variable *tvar;

	LL_FOREACH(var, tvar) {
		pkg_debug(4, "solver: add %d-ary %s clause to variable %s-%s: %d",
				nrules, desc, tvar->origin, tvar->digest, rule->inverse);
		tvar->nrules += nrules;
		head = calloc(1, sizeof (struct _pkg_solve_var_rule));
		if (head == NULL) {
			pkg_emit_errno("calloc", "_pkg_solve_var_rule");
			return (EPKG_FATAL);
		}
		head->rule = rule;
		LL_PREPEND(tvar->rules, head);
		if (!iterate_vars)
			break;
	}

	return (EPKG_OK);
}

#define RULE_ITEM_PREPEND(rule, item) do {									\
	(item)->nitems = (rule)->items ? (rule)->items->nitems + 1 : 1;			\
	LL_PREPEND((rule)->items, (item));										\
} while (0)

static int
pkg_solve_add_pkg_rule(struct pkg_jobs *j, struct pkg_solve_problem *problem,
		struct pkg_solve_variable *pvar, bool conflicting)
{
	struct pkg_dep *dep, *dtmp;
	struct pkg_conflict *conflict, *ctmp, *cfound;
	struct pkg *pkg;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it = NULL;
	struct pkg_solve_variable *var, *tvar, *cur_var;
	int cnt;

	const char *origin;

	/* Go through all deps in all variables*/
	LL_FOREACH(pvar, cur_var) {
		pkg = cur_var->unit->pkg;
		HASH_ITER(hh, pkg->deps, dep, dtmp) {
			rule = NULL;
			it = NULL;
			var = NULL;

			origin = pkg_dep_get(dep, PKG_DEP_ORIGIN);
			HASH_FIND(ho, problem->variables_by_origin, origin, strlen(origin), var);
			if (var == NULL) {
				if (pkg_solve_add_universe_variable(j, problem, origin, &var) != EPKG_OK)
					continue;
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
			RULE_ITEM_PREPEND(rule, it);
			/* B1 | B2 | ... */
			cnt = 1;
			LL_FOREACH(var, tvar) {
				it = pkg_solve_item_new(tvar);
				if (it == NULL)
					goto err;

				it->inverse = false;
				RULE_ITEM_PREPEND(rule, it);
				cnt ++;
			}
			pkg_solve_add_var_rules (var, rule->items, cnt, true, "dependency");
			pkg_solve_add_var_rules (cur_var, rule->items, cnt, false, "dependency");

			LL_PREPEND(problem->rules, rule);
			problem->rules_count ++;
		}

		/* Go through all conflicts */
		HASH_ITER(hh, pkg->conflicts, conflict, ctmp) {
			rule = NULL;
			it = NULL;
			var = NULL;

			origin = pkg_conflict_origin(conflict);
			HASH_FIND(ho, problem->variables_by_origin, origin, strlen(origin), var);
			if (var == NULL) {
				if (pkg_solve_add_universe_variable(j, problem, origin, &var) != EPKG_OK)
					continue;
			}
			/* Return the origin to the package's origin and not conflict */
			pkg_get(pkg, PKG_ORIGIN, &origin);
			/* Add conflict rule from each of the alternative */
			LL_FOREACH(var, tvar) {
				HASH_FIND_STR(tvar->unit->pkg->conflicts, origin, cfound);
				if (cfound == NULL) {
					/* Skip non-mutual conflicts */
					continue;
				}
				/* Conflict rule: (!A | !Bx) */
				rule = pkg_solve_rule_new();
				if (rule == NULL)
					goto err;
				/* !A */
				it = pkg_solve_item_new(cur_var);
				if (it == NULL)
					goto err;

				it->inverse = true;
				RULE_ITEM_PREPEND(rule, it);
				/* !Bx */
				it = pkg_solve_item_new(tvar);
				if (it == NULL)
					goto err;

				it->inverse = true;
				RULE_ITEM_PREPEND(rule, it);

				LL_PREPEND(problem->rules, rule);
				problem->rules_count ++;
				pkg_solve_add_var_rules (tvar, rule->items, 2, false,
						"explicit conflict");
				pkg_solve_add_var_rules (cur_var, rule->items, 2, false,
						"explicit conflict");
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
					RULE_ITEM_PREPEND(rule, it);
					/* !Ay */
					it = pkg_solve_item_new(tvar);
					if (it == NULL)
						goto err;

					it->inverse = true;
					RULE_ITEM_PREPEND(rule, it);

					LL_PREPEND(problem->rules, rule);
					problem->rules_count ++;

					pkg_solve_add_var_rules (tvar, rule->items, 2, false, "chain conflict");
					pkg_solve_add_var_rules (cur_var, rule->items, 2, false, "chain conflict");
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
		if (jreq->skip)
			continue;

		rule = NULL;
		it = NULL;
		var = NULL;

		var = pkg_solve_variable_new(jreq->item);
		if (var == NULL)
			goto err;

		pkg_debug(4, "solver: add variable from install request with origin %s-%s",
						var->origin, var->digest);
		HASH_ADD_KEYPTR(hd, problem->variables_by_digest, var->digest, strlen(var->digest), var);
		HASH_FIND(ho, problem->variables_by_origin, var->origin, strlen(var->origin), tvar);
		if (tvar == NULL) {
			HASH_ADD_KEYPTR(ho, problem->variables_by_origin, var->origin, strlen(var->origin), var);
		}
		else {
			DL_APPEND(tvar, var);
		}
		it = pkg_solve_item_new(var);
		if (it == NULL)
			goto err;

		rule = pkg_solve_rule_new();
		if (rule == NULL)
			goto err;

		/* Requests are unary rules */
		RULE_ITEM_PREPEND(rule, it);
		pkg_solve_add_var_rules (var, it, 1, false, "unary add");
		LL_PREPEND(problem->rules, rule);
		problem->rules_count ++;
	}
	HASH_ITER(hh, j->request_delete, jreq, jtmp) {
		if (jreq->skip)
			continue;

		rule = NULL;
		it = NULL;
		var = NULL;

		var = pkg_solve_variable_new(jreq->item);
		if (var == NULL)
			goto err;

		pkg_debug(4, "solver: add variable from delete request with origin %s-%s",
				var->origin, var->digest);
		HASH_ADD_KEYPTR(hd, problem->variables_by_digest, var->digest, strlen(var->digest), var);
		HASH_FIND(ho, problem->variables_by_origin, var->origin, strlen(var->origin), tvar);
		if (tvar == NULL) {
			HASH_ADD_KEYPTR(ho, problem->variables_by_origin, var->origin, strlen(var->origin), var);
		}
		else {
			DL_APPEND(tvar, var);
		}
		it = pkg_solve_item_new(var);
		if (it == NULL)
			goto err;

		it->inverse = true;
		rule = pkg_solve_rule_new();
		if (rule == NULL)
			goto err;

		/* Requests are unary rules */
		RULE_ITEM_PREPEND(rule, it);
		LL_PREPEND(problem->rules, rule);
		pkg_solve_add_var_rules (var, it, 1, false, "unary del");
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
				var = pkg_solve_variable_new(ucur);
				if (var == NULL)
					goto err;
				HASH_ADD_KEYPTR(hd, problem->variables_by_digest,
						var->digest, strlen(var->digest), var);

				/* Check origin */
				HASH_FIND(ho, problem->variables_by_origin, origin, strlen(origin), tvar);
				if (tvar == NULL) {
					pkg_debug(4, "solver: add variable from universe with origin %s", var->origin);
					HASH_ADD_KEYPTR(ho, problem->variables_by_origin,
							var->origin, strlen(var->origin), var);
				}
				else {
					/* Insert a variable to a chain */
					DL_APPEND(tvar, var);
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
pkg_solve_insert_res_job (struct pkg_solve_variable *var,
		struct pkg_solve_problem *problem, struct pkg_jobs *j)
{
	struct pkg_solved *res, *dres;
	struct pkg_solve_variable *cur_var, *del_var = NULL, *add_var = NULL;
	int seen_add = 0, seen_del = 0;

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
	if (seen_add > 1 || seen_del > 1) {
		pkg_emit_error("internal solver error: more than two packages to install(%d) "
				"or delete(%d) from the same origin: %s", seen_add, seen_del, var->origin);
		return;
	}
	else if (seen_add != 0 || seen_del != 0) {
		res = calloc(1, sizeof(struct pkg_solved));
		if (res == NULL) {
			pkg_emit_errno("calloc", "pkg_solved");
			return;
		}
		if (seen_add == 0 && seen_del != 0) {
			res->items[0] = del_var->unit;
			res->type = PKG_SOLVED_DELETE;
			DL_APPEND(j->jobs, res);
			pkg_debug(3, "pkg_solve: schedule deletion of %s %s",
					del_var->origin, del_var->digest);
		}
		else if (seen_del == 0 && seen_add != 0) {
			res->items[0] = add_var->unit;
			res->type = PKG_SOLVED_INSTALL;
			DL_APPEND(j->jobs, res);
			pkg_debug(3, "pkg_solve: schedule installation of %s %s",
					add_var->origin, add_var->digest);
		}
		else {
			res->items[0] = add_var->unit;
			res->items[1] = del_var->unit;
			res->type = PKG_SOLVED_UPGRADE;
			DL_APPEND(j->jobs, res);
			pkg_debug(3, "pkg_solve: schedule upgrade of %s from %s to %s",
					del_var->origin, del_var->digest, add_var->digest);
		}
		j->count ++;
	}
	else {
		pkg_debug(2, "solver: ignoring package %s(%s) as its state has not been changed",
				var->origin, var->digest);
	}
}

int
pkg_solve_sat_to_jobs(struct pkg_solve_problem *problem, struct pkg_jobs *j)
{
	struct pkg_solve_variable *var, *vtmp;

	HASH_ITER(ho, problem->variables_by_origin, var, vtmp) {
		if (!var->resolved)
			return (EPKG_FATAL);

		pkg_debug(4, "solver: check variable with origin %s", var->origin);
		pkg_solve_insert_res_job(var, problem, j);
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
