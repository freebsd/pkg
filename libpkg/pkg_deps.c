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

#include "bsd_compat.h"

#include <stddef.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg_deps.h"
#include "private/xmalloc.h"
#include "utlist.h"

struct pkg_dep_formula *
pkg_deps_parse_formula(const char *in)
{
	struct pkg_dep_formula *res = NULL, *cur = NULL;
	struct pkg_dep_formula_item *cur_item = NULL;
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
			if (isspace(*p) || *p == '\0') {
				state = st_skip_spaces;

				if (p == c) {
					/* Spaces at the beginning */
					next_state = st_parse_dep_name;
				}
				else {
					/* Spaces after the name */
					cur_item = xcalloc(1, sizeof(*cur_item));
					cur_item->name = xmalloc(p - c + 1);
					strlcpy(cur_item->name, c, p - c + 1);
					next_state = st_parse_after_name;
				}
			}
			else if (*p == ',') {
				if (p == c) {
					state = st_error;
				}
				else {
					cur_item = xcalloc(1, sizeof(*cur_item));
					cur_item->name = xmalloc(p - c + 1);
					strlcpy(cur_item->name, c, p - c + 1);
					state = st_parse_after_name;
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
					else if (memcmp(c, "==", 2) == 0) {
						cur_op = VERSION_EQ;
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
					else if (*c == '=') {
						cur_op = VERSION_EQ;
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
			if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' ||
					(*p == ',' && isdigit(*(p + 1)))) {
				p ++;
			}
			else {
				if (p - c > 0) {
					cur_ver = xcalloc(1, sizeof(*cur_ver));
					cur_ver->ver = xmalloc(p - c + 1);
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
			cur_opt = xcalloc(1, sizeof(*cur_opt));
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
					cur_opt->opt = xmalloc(p - c + 1);
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
				cur = xcalloc(1, sizeof(*cur));
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
				cur = xcalloc(1, sizeof(*cur));
			}

			DL_APPEND(cur->items, cur_item);
			cur_item = NULL;
			p ++;
			state = st_skip_spaces;
			next_state = st_parse_dep_name;
			break;

		case st_skip_spaces:
			if (isspace(*p)) {
				p ++;
			}
			else if (*p == '\0') {
				state = st_parse_comma;
			}
			else {
				c = p;
				state = next_state;
			}
			break;

		case st_error:
		default:
			pkg_emit_error("cannot parse pkg formula: %s", in);
			pkg_deps_formula_free(res);
			if (cur_item != NULL) {
				free(cur_item->name);
				free(cur_item);
			}

			return (NULL);

			break;
		}
	}

	if (state != st_skip_spaces && state != st_parse_comma) {
		pkg_emit_error("cannot parse pkg formula: %s", in);
		pkg_deps_formula_free(res);
		if (cur_item != NULL)  {
			free(cur_item->name);
			free(cur_item);
		}

		return (NULL);
	}

	return (res);
}

void
pkg_deps_formula_free(struct pkg_dep_formula *f)
{
	struct pkg_dep_formula *cf, *cftmp;
	struct pkg_dep_formula_item *cit, *cittmp;
	struct pkg_dep_version_item *cver, *cvertmp;
	struct pkg_dep_option_item *copt, *copttmp;

	DL_FOREACH_SAFE(f, cf, cftmp) {
		DL_FOREACH_SAFE(cf->items, cit, cittmp) {
			free(cit->name);

			DL_FOREACH_SAFE(cit->versions, cver, cvertmp) {
				free(cver->ver);
				free(cver);
			}

			DL_FOREACH_SAFE(cit->options, copt, copttmp) {
				free(copt->opt);
				free(copt);
			}

			free(cit);
		}

		free(cf);
	}
}

static const char*
pkg_deps_op_tostring(enum pkg_dep_version_op op)
{
	const char *op_str;

	switch (op) {
	case VERSION_ANY:
	default:
		op_str = "?";
		break;
	case VERSION_EQ:
		op_str = "=";
		break;
	case VERSION_LE:
		op_str = "<=";
		break;
	case VERSION_GE:
		op_str = ">=";
		break;
	case VERSION_LT:
		op_str = "<";
		break;
	case VERSION_GT:
		op_str = ">";
		break;
	case VERSION_NOT:
		op_str = "!=";
		break;
	}

	return (op_str);
}

char*
pkg_deps_formula_tostring(struct pkg_dep_formula *f)
{
	struct pkg_dep_formula *cf, *cftmp;
	struct pkg_dep_formula_item *cit, *cittmp;
	struct pkg_dep_version_item *cver, *cvertmp;
	struct pkg_dep_option_item *copt, *copttmp;
	char *res = NULL, *p;

	int rlen = 0, r;

	DL_FOREACH_SAFE(f, cf, cftmp) {
		DL_FOREACH_SAFE(cf->items, cit, cittmp) {
			rlen += strlen(cit->name);

			DL_FOREACH_SAFE(cit->versions, cver, cvertmp) {
				rlen += strlen(cver->ver);
				rlen += 4; /* <OP><SP><VER><SP> */
			}

			DL_FOREACH_SAFE(cit->options, copt, copttmp) {
				rlen += strlen(copt->opt);
				rlen += 2; /* <+-><OPT><SP> */
			}

			rlen += 2; /* |<SP> */
		}

		rlen += 2; /* <,><SP> */
	}

	if (rlen == 0) {
		return (NULL);
	}

	res = xmalloc(rlen + 1);

	p = res;

	DL_FOREACH_SAFE(f, cf, cftmp) {
		DL_FOREACH_SAFE(cf->items, cit, cittmp) {
			r = snprintf(p, rlen, "%s", cit->name);
			p += r;
			rlen -= r;

			DL_FOREACH_SAFE(cit->versions, cver, cvertmp) {
				r = snprintf(p, rlen, " %s %s", pkg_deps_op_tostring(cver->op),
						cver->ver);
				p += r;
				rlen -= r;
			}

			DL_FOREACH_SAFE(cit->options, copt, copttmp) {
				r = snprintf(p, rlen, " %c%s", copt->on ? '+' : '-', copt->opt);
				p += r;
				rlen -= r;
			}

			r = snprintf(p, rlen, "%s", cit->next ? " | " : "");
			p += r;
			rlen -= r;
		}

		r = snprintf(p, rlen, "%s", cf->next ? ", " : "");
		p += r;
		rlen -= r;
	}

	return (res);
}

char*
pkg_deps_formula_tosql(struct pkg_dep_formula_item *f)
{
	struct pkg_dep_formula_item *cit, *cittmp;
	struct pkg_dep_version_item *cver, *cvertmp;
	char *res = NULL, *p;

	int rlen = 0, r;

	DL_FOREACH_SAFE(f, cit, cittmp) {
		rlen += sizeof("AND (name='' )");
		rlen += strlen(cit->name);

		DL_FOREACH_SAFE(cit->versions, cver, cvertmp) {
			rlen += sizeof(" AND vercmp(>=, version,'') ");
			rlen += strlen(cver->ver);
		}

		rlen += sizeof(" OR ");
	}

	if (rlen == 0) {
		return (NULL);
	}

	res = xmalloc(rlen + 1);

	p = res;

	DL_FOREACH_SAFE(f, cit, cittmp) {
		r = snprintf(p, rlen, "(name='%s'", cit->name);
		p += r;
		rlen -= r;

		DL_FOREACH_SAFE(cit->versions, cver, cvertmp) {
			r = snprintf(p, rlen, " AND vercmp('%s',version,'%s')",
					pkg_deps_op_tostring(cver->op),
					cver->ver);
			p += r;
			rlen -= r;
		}
		r = snprintf(p, rlen, ")%s", cit->next ? " OR " : "");
		p += r;
		rlen -= r;
	}

	return (res);
}

enum pkg_dep_version_op
pkg_deps_string_toop(const char *in)
{
	enum pkg_dep_version_op ret = VERSION_ANY;
	int len;

	if (in != NULL) {
		len = strlen(in);

		if (len == 2) {
			if (memcmp(in, ">=", 2) == 0) {
				ret = VERSION_GE;
			}
			else if (memcmp(in, "<=", 2) == 0) {
				ret = VERSION_LE;
			}
			else if (memcmp(in, "!=", 2) == 0) {
				ret = VERSION_NOT;
			}
			else if (memcmp(in, "==", 2) == 0) {
				ret = VERSION_EQ;
			}
		}
		else if (len == 1) {
			if (*in == '>') {
				ret = VERSION_GT;
			}
			else if (*in == '<') {
				ret = VERSION_LT;
			}
			else if (*in == '!') {
				ret = VERSION_NOT;
			}
			else if (*in == '=') {
				ret = VERSION_EQ;
			}
		}
	}

	return (ret);
}
