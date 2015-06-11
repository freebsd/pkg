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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <stddef.h>
#include <ctype.h>
#include <assert.h>

#include "pkg.h"
#include "private/pkg.h"
#include "pkg_deps.h"


struct pkg_dep_formula *
pkg_deps_parse_formula(const char *in)
{
	struct pkg_dep_formula *res = NULL, *cur = NULL;
	struct pkg_dep_formula_item *items = NULL, *cur_item = NULL;
	struct pkg_dep_version_item *cur_ver = NULL;
	struct pkg_dep_option_item *cur_opt = NULL;
	const char *p, *c, *end;
	enum pkg_dep_version_op cur_op = VERSION_ANY;
	enum {
		st_parse_dep_name = 0,
		st_parse_after_name,
		st_parse_ver_op,
		st_parse_after_op,
		st_parse_version_number,
		st_parse_after_version,
		st_parse_option_start,
		st_parse_option,
		st_parse_after_option,
		st_parse_comma,
		st_parse_or,
		st_skip_spaces,
		st_error
	} state = 0, next_state = 0;

	c = in;
	p = in;

	end = p + strlen(p);

	while (p <= end) {
		switch (state) {
		case st_parse_dep_name:
			if (isspace(*p)) {
				state = st_skip_spaces;

				if (p == c) {
					/* Spaces at the beginning */
					next_state = st_parse_dep_name;
				}
				else {
					/* Spaces after the name */
					cur_item = calloc(1, sizeof(*cur_item));

					if (cur_item == NULL) {
						pkg_emit_errno("malloc", "struct pkg_dep_formula_item");

						return (NULL);
					}
					cur_item->name = malloc(p - c + 1);

					if (cur_item->name == NULL) {
						pkg_emit_errno("malloc", "cur->name");

						return (NULL);
					}

					strlcpy(cur_item->name, c, p - c + 1);
					next_state = st_parse_after_name;
				}
			}
			else if (!isprint(*p)) {
				state = st_error;
			}
			else {
				p++;
			}
			break;

		case st_parse_after_name:
		case st_parse_after_version:
		case st_parse_after_option: {
			switch (*p) {
			case ',':
			case '\0':
				state = st_parse_comma;
				break;
			case '|':
				state = st_parse_or;
				break;
			case '+':
			case '-':
				c = p;
				state = st_parse_option_start;
				break;
			case '>':
			case '<':
			case '=':
			case '!':
				c = p;
				cur_op = VERSION_ANY;
				state = st_parse_ver_op;
				break;
			default:
				state = st_error;
				break;
			}
			break;
		}

		case st_parse_ver_op: {
			switch (*p) {
			case '>':
			case '<':
			case '=':
			case '!':
				p ++;
				break;
			default:
				if (p - c == 2) {
					if (memcmp(c, ">=", 2) == 0) {
						cur_op = VERSION_GE;
					}
					else if (memcmp(c, "<=", 2) == 0) {
						cur_op = VERSION_LE;
					}
					else if (memcmp(c, "!=", 2) == 0) {
						cur_op = VERSION_NOT;
					}
					else {
						state = st_error;
					}
				}
				else if (p - c == 1) {
					if (*c == '>') {
						cur_op = VERSION_GT;
					}
					else if (*c == '<') {
						cur_op = VERSION_LT;
					}
					else if (*c == '!') {
						cur_op = VERSION_NOT;
					}
					else {
						state = st_error;
					}
				}
				else {
					state = st_error;
				}

				if (state != st_error) {
					state = st_skip_spaces;
					next_state = st_parse_after_op;
				}
				break;
			}
			break;
		}

		case st_parse_after_op:
			if (cur_op == VERSION_ANY) {
				state = st_error;
			}
			else {
				state = st_parse_version_number;
			}
			break;

		case st_parse_version_number:
			if (isalnum(*p) || *p == '-' || *p == '_') {
				p ++;
			}
			else {
				if (p - c > 0) {
					cur_ver = calloc(1, sizeof(*cur_ver));

					if (cur_ver == NULL) {
						pkg_emit_errno("malloc", "struct pkg_dep_version");

						return (NULL);
					}
					cur_ver->ver = malloc(p - c + 1);

					if (cur_ver->ver == NULL) {
						pkg_emit_errno("malloc", "cur_ver->ver");

						return (NULL);
					}

					strlcpy(cur_ver->ver, c, p - c + 1);
					cur_ver->op = cur_op;
					assert(cur_item != NULL);
					DL_APPEND(cur_item->versions, cur_ver);
					state = st_skip_spaces;
					next_state = st_parse_after_version;
				}
				else {
					state = st_error;
				}
			}
			break;

		case st_parse_option_start:
			cur_opt = calloc(1, sizeof(*cur_opt));
			if (cur_ver == NULL) {
				pkg_emit_errno("malloc", "struct pkg_dep_option");

				return (NULL);
			}

			if (*p == '+') {
				cur_opt->on = true;
			}
			else {
				cur_opt->on = false;
			}

			p ++;
			c = p;
			state = st_parse_option;
			break;

		case st_parse_option:
			if (isalnum(*p) || *p == '-' || *p == '_') {
				p ++;
			}
			else {
				if (p - c > 0) {
					cur_opt->opt = malloc(p - c + 1);

					if (cur_opt->opt == NULL) {
						pkg_emit_errno("malloc", "cur_opt->opt");

						return (NULL);
					}

					strlcpy(cur_opt->opt, c, p - c + 1);
					assert(cur_item != NULL);
					DL_APPEND(cur_item->options, cur_opt);
					state = st_skip_spaces;
					next_state = st_parse_after_option;
				}
				else {
					state = st_error;
				}
			}
			break;

		case st_parse_comma:
			assert(cur_item != NULL);

			if (cur == NULL) {
				cur = calloc(1, sizeof(*cur));

				if (cur == NULL) {
					pkg_emit_errno("malloc", "struct pkg_dep_formula");

					return (NULL);
				}
			}

			DL_APPEND(cur->items, cur_item);
			DL_APPEND(res, cur);
			cur_item = NULL;
			cur = NULL;
			p ++;
			state = st_skip_spaces;
			next_state = st_parse_dep_name;
			break;

		case st_parse_or:
			assert(cur_item != NULL);

			if (cur == NULL) {
				cur = calloc(1, sizeof(*cur));

				if (cur == NULL) {
					pkg_emit_errno("malloc", "struct pkg_dep_formula");

					return (NULL);
				}
			}

			DL_APPEND(cur->items, cur_item);
			cur_item = NULL;
			p ++;
			state = st_skip_spaces;
			next_state = st_parse_dep_name;
			break;

		case st_skip_spaces:
			if (isspace(*p) || *p == '\0') {
				p ++;
			}
			else {
				c = p;
				state = next_state;
			}
			break;

		case st_error:
		default:
			pkg_emit_error ("cannot parse pkg formula: %s", in);
			pkg_deps_formula_free (res);

			return (NULL);

			break;
		}
	}

	if (state != st_skip_spaces) {
		pkg_emit_error ("cannot parse pkg formula: %s", in);
		pkg_deps_formula_free (res);

		return (NULL);
	}

	return (res);
}
