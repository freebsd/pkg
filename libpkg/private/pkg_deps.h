/*-
 * Copyright (c) 2015, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR ''AS IS'' AND ANY
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


#ifndef LIBPKG_PRIVATE_PKG_DEPS_H_
#define LIBPKG_PRIVATE_PKG_DEPS_H_

#include <stdbool.h>

enum pkg_dep_version_op {
	VERSION_ANY = 0,
	VERSION_EQ,
	VERSION_GE,
	VERSION_LE,
	VERSION_LT,
	VERSION_GT,
	VERSION_NOT,
};

enum pkg_dep_flag {
	PKG_DEP_FLAG_NORMAL = 0,
	PKG_DEP_FLAG_REQUIRE = (1 << 0),
	PKG_DEP_FLAG_GLOB = (1 << 1),
	PKG_DEP_FLAG_REGEXP = (1 << 2)
};

struct pkg_dep_version_item {
	char *ver;
	enum pkg_dep_version_op op;
	struct pkg_dep_version_item *prev, *next;
};

struct pkg_dep_option_item {
	char *opt;
	bool on;
	struct pkg_dep_option_item *prev, *next;
};

struct pkg_dep_formula_item {
	char *name;
	unsigned flags;
	struct pkg_dep_version_item *versions;
	struct pkg_dep_option_item *options;

	struct pkg_dep_formula_item *prev, *next;
};

struct pkg_dep_formula {
	struct pkg_dep_formula_item *items;
	struct pkg_dep_formula *prev, *next;
};


/*
 * Convert pkg formula string to the pkg_dep_formula linked list
 */
struct pkg_dep_formula* pkg_deps_parse_formula(const char *in);

void pkg_deps_formula_free(struct pkg_dep_formula *f);

char* pkg_deps_formula_tostring(struct pkg_dep_formula *f);

enum pkg_dep_version_op pkg_deps_string_toop(const char *in);

char* pkg_deps_formula_tosql(struct pkg_dep_formula_item *f);

#endif /* LIBPKG_PRIVATE_PKG_DEPS_H_ */
