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
#include <math.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

struct pkg_solve_item;

struct pkg_solve_variable {
	struct pkg_job_universe_item *unit;
	bool to_install;
	int guess;
	int priority;
	const char *digest;
	const char *uid;
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
	struct pkg_jobs *j;
	unsigned int rules_count;
	struct pkg_solve_rule *rules;
	struct pkg_solve_variable *variables_by_uid;
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

static void
pkg_debug_print_rule (struct pkg_solve_item *rule)
{
	struct pkg_solve_item *it;
	struct sbuf *sb;
	int64_t expectlevel;

	/* Avoid expensive printing if debug level is less than required */
	expectlevel = pkg_object_int(pkg_config_get("DEBUG_LEVEL"));

	if (expectlevel < 2)
		return;

	sb = sbuf_new_auto();

	sbuf_printf(sb, "%s", "rule: (");

	LL_FOREACH(rule, it) {
		if (it->var->resolved) {
			sbuf_printf(sb, "%s%s%s(%c)%s", it->inverse ? "!" : "",
					it->var->uid,
					(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)",
					(it->var->to_install) ? '+' : '-',
					it->next ? " | " : ")");
		}
		else {
			sbuf_printf(sb, "%s%s%s%s", it->inverse ? "!" : "",
					it->var->uid,
					(it->var->unit->pkg->type == PKG_INSTALLED) ? "(l)" : "(r)",
					it->next ? " | " : ")");
		}
	}
	sbuf_finish(sb);
	pkg_debug(2, "%s", sbuf_data(sb));
	sbuf_delete(sb);
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
							if (PKG_SOLVE_CHECK_ITEM(it)) {
								ret = true;
								break;
							}
						}
					}
					if (!ret) {
						struct sbuf *err_msg = sbuf_new_auto();
						sbuf_printf(err_msg, "cannot resolve conflict between ");
						LL_FOREACH(unresolved, it) {
							sbuf_printf(err_msg, "%s %s(want %s), ",
									it->var->unit->pkg->type == PKG_INSTALLED ? "local" : "remote",
											it->var->uid,
											it->var->to_install ? "install" : "remove");
						}
						sbuf_finish(err_msg);
						pkg_emit_error("%splease resolve it manually", sbuf_data(err_msg));
						pkg_debug_print_rule(unresolved);
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
							if (PKG_SOLVE_CHECK_ITEM(it)) {
								ret = true;
								break;
							}
						}
					}
					if (!ret) {
						/* This is a unit */
						int resolved = 0;
						LL_FOREACH(unresolved, it) {
							if (!it->var->resolved) {
								it->var->to_install = (!it->inverse);
								it->var->resolved = true;
								pkg_solve_update_var_resolved(it->var);
								pkg_debug(2, "propagate %s-%s(%d) to %s",
										it->var->uid, it->var->digest,
										it->var->priority,
										it->var->to_install ? "install" : "delete");
								pkg_debug_print_rule(unresolved);
								resolved ++;
								break;
							}
						}
						if (resolved == 0) {
							pkg_debug_print_rule(unresolved);
							assert (resolved > 0);
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
			assert (var->rules == NULL);
			var->to_install = (var->unit->pkg->type == PKG_INSTALLED);
			var->resolved = true;
			pkg_debug(2, "leave %s-%s(%d) to %s",
					var->uid, var->digest,
					var->priority, var->to_install ? "install" : "delete");
		}
		else {
			LL_FOREACH(var->rules, rul) {
				it = rul->rule;
				if (it->nitems == 1 && it->nresolved == 0) {
					it->var->to_install = (!it->inverse);
					it->var->resolved = true;
					pkg_debug(2, "requested %s-%s(%d) to %s",
							it->var->uid, it->var->digest,
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
pkg_solve_test_guess(struct pkg_solve_problem *problem, struct pkg_solve_variable *var)
{
	bool test = false;
	struct _pkg_solve_var_rule *rul;
	struct pkg_solve_item *it, *cur;

	LL_FOREACH(var->rules, rul) {
		it = rul->rule;
		if (it->nitems != it->nresolved) {
			/* Check guess */
			test = false;
			LL_FOREACH(it, cur) {
				if (cur->var->resolved)
					test |= cur->var->to_install ^ cur->inverse;
				else if (cur->var->guess != -1)
					test |= cur->var->guess ^ cur->inverse;
				else {
					/* Free variables are assumed as true */
					test = true;
					break;
				}
			}
			if (!test) {
				pkg_debug(2, "solver: guess test failed at variable %s, trying to %d",
						var->uid, var->guess);
				pkg_debug_print_rule(it);
				return (false);
			}
		}
	}


	return (true);
}

/*
 * Set initial guess based on a variable passed
 */
static bool
pkg_solve_initial_guess(struct pkg_solve_problem *problem,
		struct pkg_solve_variable *var)
{
	if (problem->j->type == PKG_JOBS_UPGRADE) {
		if (var->unit->pkg->type == PKG_INSTALLED) {
			/* For local packages assume true if we have no upgrade */
			if (var->unit->next == NULL && var->unit->prev == var->unit)
				return (true);
		}
		else {
			/* For remote packages we return true if they are upgrades for local ones */
			if (var->unit->next != NULL || var->unit->prev != var->unit)
				return (true);
		}
	}
	else {
		/* For all non-upgrade jobs be more conservative */
		if (var->unit->pkg->type == PKG_INSTALLED)
			return (true);
	}

	/* Otherwise set initial guess to false */
	return (false);
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
	int propagated;
	struct pkg_solve_variable *var, *tvar;
	int64_t unresolved = 0, iters = 0;
	bool rc, backtrack = false;

	struct _solver_tree_elt {
		struct pkg_solve_variable *var;
		int guess;
		struct _solver_tree_elt *prev, *next;
	} *solver_tree = NULL, *elt;


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
	elt = solver_tree;

	/* DPLL Algorithm */
	DL_FOREACH2(problem->variables_by_digest, var, hd.next) {
		if (!var->resolved) {

			if (backtrack) {
				/* Shift var back */
				var = tvar;
				backtrack = false;
			}

			if (elt == NULL) {
				/* Add new element to the backtracking queue */
				elt = malloc (sizeof (*elt));
				if (elt == NULL) {
					pkg_emit_errno("malloc", "_solver_tree_elt");
					LL_FREE(solver_tree, free);
					return (false);
				}
				elt->var = var;
				elt->guess = -1;
				DL_APPEND(solver_tree, elt);
			}
			assert (var == elt->var);

			if (elt->guess == -1)
				var->guess = pkg_solve_initial_guess(problem, var);
			else
				/* For analyzed variables we can only inverse previous guess */
				var->guess = !elt->guess;

			unresolved ++;
			iters ++;
			if (!pkg_solve_test_guess(problem, var)) {
				if (elt->guess == -1) {
					/* This is free variable, so we can assign true or false to it */
					var->guess = !var->guess;
					rc = pkg_solve_test_guess(problem, var);
				}
				else {
					rc = false;
				}
				if (!rc) {
					/* Need to backtrack */
					iters ++;
					if (elt == NULL || elt->prev->next == NULL) {
						/* Cannot backtrack, UNSAT */
						pkg_debug(1, "problem is UNSAT problem after %d guesses", iters);
						LL_FREE(solver_tree, free);
						return (false);
					}
					/* Set the current variable as free variable */
					elt->guess = -1;
					var->guess = -1;
					/* Go to the previous level */
					elt = elt->prev;
					tvar = elt->var;
					backtrack = true;
					continue;
				}
			}

			/* Assign the current guess */
			elt->guess = var->guess;
			/* Move to the next elt */
			elt = elt->next;
		}
	}

	pkg_debug(1, "solved SAT problem in %d guesses", iters);

	HASH_ITER(hd, problem->variables_by_digest, var, tvar) {
		if (!var->resolved) {
			var->to_install = var->guess;
			var->resolved = true;
		}
	}

	LL_FREE(solver_tree, free);

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
	const char *digest, *uid;

	result = calloc(1, sizeof(struct pkg_solve_variable));

	if(result == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_variable");
		return (NULL);
	}

	result->unit = item;
	pkg_get(item->pkg, PKG_UNIQUEID, &uid, PKG_DIGEST, &digest);
	/* XXX: Is it safe to save a ptr here ? */
	result->digest = digest;
	result->guess = -1;
	result->uid = uid;
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
pkg_solve_add_universe_variable(struct pkg_solve_problem *problem,
		const char *uid, struct pkg_solve_variable **var)
{
	struct pkg_job_universe_item *unit, *cur;
	struct pkg_solve_variable *nvar, *tvar = NULL, *found;
	const char *digest;
	struct pkg_jobs *j = problem->j;

	HASH_FIND_STR(j->universe, uid, unit);
	/* If there is no package in universe, refuse continue */
	if (unit == NULL) {
		pkg_debug(2, "package %s is not found in universe", uid);
		return (EPKG_FATAL);
	}
	/* Need to add a variable */
	nvar = pkg_solve_variable_new(unit);
	if (nvar == NULL)
		return (EPKG_FATAL);

	HASH_ADD_KEYPTR(hd, problem->variables_by_digest, nvar->digest,
			strlen(nvar->digest), nvar);

	/*
	 * Now we check the uid variable and if there is no such uid then
	 * we need to add the whole conflict chain to it
	 */
	HASH_FIND(ho, problem->variables_by_uid, uid, strlen(uid), found);
	if (found == NULL) {
		HASH_ADD_KEYPTR(ho, problem->variables_by_uid, nvar->uid,
				strlen(nvar->uid), nvar);
		pkg_debug(4, "solver: add variable from universe with uid %s", nvar->uid);

		/* Rewind to the beginning of the list */
		while (unit->prev->next != NULL)
			unit = unit->prev;

		LL_FOREACH (unit, cur) {
			pkg_get(cur->pkg, PKG_DIGEST, &digest);
			HASH_FIND(hd, problem->variables_by_digest, digest,
					strlen(digest), found);
			if (found == NULL) {
				/* Add all alternatives as independent variables */
				tvar = pkg_solve_variable_new(cur);
				if (tvar == NULL)
					return (EPKG_FATAL);
				DL_APPEND(nvar, tvar);
				HASH_ADD_KEYPTR(hd, problem->variables_by_digest, tvar->digest,
						strlen(tvar->digest), tvar);
				pkg_debug (4, "solver: add another variable with uid %s and digest %s",
						tvar->uid, tvar->digest);
			}
		}
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
		pkg_debug(4, "solver: add %d-ary %s clause to variable %s-%s",
							nrules, desc, tvar->uid, tvar->digest);
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
	pkg_debug_print_rule(head->rule);

	return (EPKG_OK);
}

#define RULE_ITEM_PREPEND(rule, item) do {									\
	(item)->nitems = (rule)->items ? (rule)->items->nitems + 1 : 1;			\
	LL_PREPEND((rule)->items, (item));										\
} while (0)

static int
pkg_solve_handle_provide (struct pkg_solve_problem *problem,
		struct pkg_job_provide *pr, struct pkg_solve_rule *rule, int *cnt)
{
	struct pkg_solve_item *it = NULL;
	const char *uid, *digest;
	struct pkg_solve_variable *var;
	struct pkg_job_universe_item *un, *cur;
	struct pkg_shlib *sh;

	/* Find the first package in the universe list */
	un = pr->un;
	while (un->prev->next != NULL) {
		un = un->prev;
	}

	LL_FOREACH(un, cur) {
		/* For each provide */
		pkg_get(un->pkg, PKG_DIGEST, &digest, PKG_UNIQUEID, &uid);
		HASH_FIND(hd, problem->variables_by_digest, digest,
				strlen(digest), var);
		if (var == NULL) {
			if (pkg_solve_add_universe_variable(problem, uid,
					&var) != EPKG_OK)
				continue;
		}
		/* Check if we have the specified require provided by this package */
		HASH_FIND_STR(un->pkg->provides, pr->provide, sh);
		if (sh == NULL)
			continue;

		it = pkg_solve_item_new(var);
		if (it == NULL)
			return (EPKG_FATAL);

		it->inverse = false;
		RULE_ITEM_PREPEND(rule, it);
		(*cnt) ++;
	}

	return (EPKG_OK);
}

static int
pkg_solve_add_pkg_rule(struct pkg_solve_problem *problem,
		struct pkg_solve_variable *pvar, bool conflicting)
{
	struct pkg_dep *dep, *dtmp;
	struct pkg_conflict *conflict, *ctmp;
	struct pkg *pkg;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it = NULL;
	struct pkg_solve_variable *var, *tvar, *cur_var;
	struct pkg_shlib *shlib = NULL;
	struct pkg_job_provide *pr, *prhead;
	int cnt;
	struct pkg_jobs *j = problem->j;

	const char *uid;

	/* Go through all deps in all variables*/
	LL_FOREACH(pvar, cur_var) {
		pkg = cur_var->unit->pkg;
		HASH_ITER(hh, pkg->deps, dep, dtmp) {
			rule = NULL;
			it = NULL;
			var = NULL;

			uid = dep->uid;
			HASH_FIND(ho, problem->variables_by_uid, uid, strlen(uid), var);
			if (var == NULL) {
				if (pkg_solve_add_universe_variable(problem, uid, &var) != EPKG_OK)
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

			uid = pkg_conflict_uniqueid(conflict);
			HASH_FIND(ho, problem->variables_by_uid, uid, strlen(uid), var);
			if (var == NULL) {
				if (pkg_solve_add_universe_variable(problem, uid, &var) != EPKG_OK)
					continue;
			}
			/* Return the uid to the package's uid and not conflict */
			pkg_get(pkg, PKG_UNIQUEID, &uid);
			/* Add conflict rule from each of the alternative */
			LL_FOREACH(var, tvar) {
				if (conflict->type == PKG_CONFLICT_REMOTE_LOCAL) {
					/* Skip unappropriate packages */
					if (pkg->type == PKG_INSTALLED) {
						if (tvar->unit->pkg->type == PKG_INSTALLED)
							continue;
					}
					else {
						if (tvar->unit->pkg->type != PKG_INSTALLED)
							continue;
					}
				}
				else if (conflict->type == PKG_CONFLICT_REMOTE_REMOTE) {
					if (pkg->type == PKG_INSTALLED)
						continue;

					if (tvar->unit->pkg->type == PKG_INSTALLED)
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

		/* Check required shlibs */
		shlib = NULL;
		if (pkg->type != PKG_INSTALLED) {
			while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
				rule = NULL;
				it = NULL;
				var = NULL;
				HASH_FIND_STR(j->provides, pkg_shlib_name(shlib), prhead);
				if (prhead != NULL) {
					/* Require rule !A | P1 | P2 | P3 ... */
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
					LL_FOREACH(prhead, pr) {
						if (pkg_solve_handle_provide (problem, pr, rule,
								&cnt) != EPKG_OK)
							goto err;
					}

					if (cnt > 1) {
						pkg_solve_add_var_rules (var, rule->items, cnt, true, "provide");
						pkg_solve_add_var_rules (cur_var, rule->items, cnt, false, "provide");

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
							pkg_shlib_name(shlib));
				}
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

static int
pkg_solve_add_universe_item(struct pkg_job_universe_item *un,
		struct pkg_solve_problem *problem)
{
	struct pkg_job_universe_item *ucur;
	struct pkg_solve_variable *var = NULL, *tvar;
	const char *uid, *digest;

	/* Rewind universe pointer */
	while (un->prev->next != NULL)
		un = un->prev;

	LL_FOREACH(un, ucur) {
		pkg_get(ucur->pkg, PKG_UNIQUEID, &uid, PKG_DIGEST, &digest);
		HASH_FIND(hd, problem->variables_by_digest, digest, strlen(digest), var);
		if (var == NULL) {
			/* Add new variable */
			var = pkg_solve_variable_new(ucur);
			if (var == NULL)
				return (EPKG_FATAL);
			HASH_ADD_KEYPTR(hd, problem->variables_by_digest,
					var->digest, strlen(var->digest), var);

			/* Check uid */
			HASH_FIND(ho, problem->variables_by_uid, uid, strlen(uid), tvar);
			if (tvar == NULL) {
				pkg_debug(4, "solver: add variable from universe with uid %s", var->uid);
				HASH_ADD_KEYPTR(ho, problem->variables_by_uid,
						var->uid, strlen(var->uid), var);
			}
			else {
				/* Insert a variable to a chain */
				DL_APPEND(tvar, var);
			}
		}
	}
	HASH_FIND(ho, problem->variables_by_uid, uid, strlen(uid), var);
	/* Now `var' contains a variables chain related to this uid */
	if (pkg_solve_add_pkg_rule(problem, var, true) == EPKG_FATAL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

struct pkg_solve_problem *
pkg_solve_jobs_to_sat(struct pkg_jobs *j)
{
	struct pkg_solve_problem *problem;
	struct pkg_job_request *jreq, *jtmp;
	struct pkg_solve_rule *rule;
	struct pkg_solve_item *it;
	struct pkg_job_universe_item *un, *utmp;
	struct pkg_solve_variable *var;
	const char *digest;

	problem = calloc(1, sizeof(struct pkg_solve_problem));

	if (problem == NULL) {
		pkg_emit_errno("calloc", "pkg_solve_problem");
		return (NULL);
	}

	problem->j = j;

	/* Add requests */
	HASH_ITER(hh, j->request_add, jreq, jtmp) {
		if (jreq->skip)
			continue;

		rule = NULL;
		it = NULL;
		var = NULL;

		if (pkg_solve_add_universe_item(jreq->item, problem) == EPKG_FATAL)
			goto err;

		pkg_get(jreq->item->pkg, PKG_DIGEST, &digest);
		HASH_FIND(hd, problem->variables_by_digest, digest, strlen(digest), var);

		if (var == NULL) {
			pkg_emit_error("solver: variable has not been added, internal error");
			goto err;
		}

		pkg_debug(4, "solver: add variable from install request with uid %s-%s",
						var->uid, var->digest);

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

		if (pkg_solve_add_universe_item(jreq->item, problem) == EPKG_FATAL)
			goto err;

		pkg_get(jreq->item->pkg, PKG_DIGEST, &digest);
		HASH_FIND(hd, problem->variables_by_digest, digest, strlen(digest), var);

		if (var == NULL) {
			pkg_emit_error("solver: variable has not been added, internal error");
			goto err;
		}

		pkg_debug(4, "solver: add variable from delete request with uid %s-%s",
				var->uid, var->digest);

		it = pkg_solve_item_new(var);
		if (it == NULL)
			goto err;

		it->inverse = true;

		rule = pkg_solve_rule_new();
		if (rule == NULL)
			goto err;

		/* Requests are unary rules */
		RULE_ITEM_PREPEND(rule, it);
		pkg_solve_add_var_rules (var, it, 1, false, "unary delete");
		LL_PREPEND(problem->rules, rule);
		problem->rules_count ++;
	}

	/* Parse universe */
	HASH_ITER(hh, j->universe, un, utmp) {
		rule = NULL;
		it = NULL;
		var = NULL;

		/* Add corresponding variables */
		if (pkg_solve_add_universe_item(un, problem) == EPKG_FATAL)
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
	if (seen_add > 1 || seen_del > 1) {
		pkg_emit_error("internal solver error: more than two packages to install(%d) "
				"or delete(%d) from the same uid: %s", seen_add, seen_del, var->uid);
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
					del_var->uid, del_var->digest);
		}
		else if (seen_del == 0 && seen_add != 0) {
			res->items[0] = add_var->unit;
			res->type = (j->type == PKG_JOBS_FETCH) ?
					PKG_SOLVED_FETCH : PKG_SOLVED_INSTALL;
			DL_APPEND(j->jobs, res);
			pkg_debug(3, "pkg_solve: schedule installation of %s %s",
					add_var->uid, add_var->digest);
		}
		else {
			res->items[0] = add_var->unit;
			res->items[1] = del_var->unit;
			res->type = PKG_SOLVED_UPGRADE;
			DL_APPEND(j->jobs, res);
			pkg_debug(3, "pkg_solve: schedule upgrade of %s from %s to %s",
					del_var->uid, del_var->digest, add_var->digest);
		}
		j->count ++;
	}
	else {
		pkg_debug(2, "solver: ignoring package %s(%s) as its state has not been changed",
				var->uid, var->digest);
	}
}

int
pkg_solve_sat_to_jobs(struct pkg_solve_problem *problem)
{
	struct pkg_solve_variable *var, *vtmp;

	HASH_ITER(ho, problem->variables_by_uid, var, vtmp) {
		if (!var->resolved)
			return (EPKG_FATAL);

		pkg_debug(4, "solver: check variable with uid %s", var->uid);
		pkg_solve_insert_res_job(var, problem);
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
		ret = pkg_solve_sat_to_jobs(problem);
	else {
		pkg_emit_error("cannot parse sat solver output");
		ret = EPKG_FATAL;
	}

	HASH_FREE(ordered_variables, free);
	if (line != NULL)
		free(line);
	return (ret);
}
