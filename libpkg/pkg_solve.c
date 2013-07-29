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


/* Used as an atom in rules */
struct pkg_solve_item {
	struct pkg *pkg;
	bool to_install;
	bool inverse;
	bool resolved;
	struct pkg_solve_item *next;
};

struct pkg_solve_rule {
	struct pkg_solve_item *items;
	bool resolved;
	struct pkg_solve_rule *next;
};

/*
 * Use XOR here to implement the following logic:
 * atom is true if it is installed and not inverted or
 * if it is not installed but inverted
 */
#define PKG_SOLVE_CHECK_ITEM(item)				\
	((item)->to_install ^ (item)->inverse)

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
		if (cur->resolved)
			continue;

		ret = false;
		LL_FOREACH(cur->items, it) {
			if (it->resolved) {
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
		if (cur->resolved)
			continue;

		LL_FOREACH(cur->items, it) {
			if (it->resolved && PKG_SOLVE_CHECK_ITEM(it))
				resolved++;
			else
				unresolved = it;
			total ++;
		}
		/* It is a unit */
		if (total == resolved + 1 && unresolved != NULL) {
			if (!unresolved->resolved) {
				/* Propagate unit */
				unresolved->resolved = true;
				unresolved->to_install = !unresolved->inverse;
				cur->resolved = true;
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
		if (cur->resolved)
			continue;

		it = cur->items;
		/* Unary rules */
		if (!it->resolved && it->next == NULL) {
			it->to_install = !it->inverse;
			it->resolved = true;
			cur->resolved = true;
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
			if (it->resolved ^ next->resolved) {
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
			if (!it->resolved) {
				if (it->pkg->type == PKG_INSTALLED) {
					it->to_install = true;
					if (pkg_solve_check_conflicts(rules))
						it->resolved = true;
				}
				else {
					it->to_install = false;
					if (pkg_solve_check_conflicts(rules))
						it->resolved = true;
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
pkg_solve_sat_problem(struct pkg_solve_rule *rules)
{

	/* Initially propagate explicit rules */
	while (pkg_solve_propagate_explicit(rules));

	/* Now try to assign default values */
	pkg_solve_propagate_default(rules);

	while (!pkg_solve_check_rules(rules)) {
		/* TODO:
		 * 1) assign a free variable
		 * 2) check for contradictions
		 * 3) analyse and learn
		 * 4) undo an assignment
		 */
	}

	return (true);
}
