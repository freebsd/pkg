/*-
 * Copyright (c) 2015-2017, Vsevolod Stakhov
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
#include "xmalloc.h"

dep_formulav_t *
pkg_deps_parse_formula(const char *in)
{
	dep_formulav_t *res = NULL;
	struct pkg_dep_formula cur_formula;
	struct pkg_dep_formula_item cur_item;
	struct pkg_dep_version_item cur_ver;
	struct pkg_dep_option_item cur_opt;
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

	if (in == NULL || *in == '\0')
		return (NULL);

	end = p + strlen(p);

	res = xcalloc(1, sizeof(*res));
	memset(&cur_formula, 0, sizeof(cur_formula));
	memset(&cur_item, 0, sizeof(cur_item));
	memset(&cur_ver, 0, sizeof(cur_ver));
	memset(&cur_opt, 0, sizeof(cur_opt));

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
					cur_item.name = xmalloc(p - c + 1);
					strlcpy(cur_item.name, c, p - c + 1);
					next_state = st_parse_after_name;
				}
			}
			else if (*p == ',') {
				if (p == c) {
					state = st_error;
				}
				else {
					cur_item.name = xmalloc(p - c + 1);
					strlcpy(cur_item.name, c, p - c + 1);
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
					memset(&cur_ver, 0, sizeof(cur_ver));
					cur_ver.ver = xmalloc(p - c + 1);
					strlcpy(cur_ver.ver, c, p - c + 1);
					cur_ver.op = cur_op;
					assert(cur_item.name != NULL);
					vec_push(&cur_item.versions, cur_ver);
					state = st_skip_spaces;
					next_state = st_parse_after_version;
				}
				else {
					state = st_error;
				}
			}
			break;

		case st_parse_option_start:
			memset(&cur_opt, 0, sizeof(cur_opt));
			if (*p == '+') {
				cur_opt.on = true;
			}
			else {
				cur_opt.on = false;
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
					cur_opt.opt = xmalloc(p - c + 1);
					strlcpy(cur_opt.opt, c, p - c + 1);
					assert(cur_item.name != NULL);
					vec_push(&cur_item.options, cur_opt);
					state = st_skip_spaces;
					next_state = st_parse_after_option;
				}
				else {
					state = st_error;
				}
			}
			break;

		case st_parse_comma:
			assert(cur_item.name != NULL);

			vec_push(&cur_formula.items, cur_item);
			memset(&cur_item, 0, sizeof(cur_item));
			vec_push(res, cur_formula);
			memset(&cur_formula, 0, sizeof(cur_formula));
			p ++;
			state = st_skip_spaces;
			next_state = st_parse_dep_name;
			break;

		case st_parse_or:
			assert(cur_item.name != NULL);

			vec_push(&cur_formula.items, cur_item);
			memset(&cur_item, 0, sizeof(cur_item));
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
			free(cur_item.name);
			vec_foreach(cur_item.versions, vi)
				free(cur_item.versions.d[vi].ver);
			vec_free(&cur_item.versions);
			vec_foreach(cur_item.options, oi)
				free(cur_item.options.d[oi].opt);
			vec_free(&cur_item.options);
			/* Free any accumulated items in cur_formula */
			vec_foreach(cur_formula.items, ii) {
				struct pkg_dep_formula_item *it = &cur_formula.items.d[ii];
				free(it->name);
				vec_foreach(it->versions, vi)
					free(it->versions.d[vi].ver);
				vec_free(&it->versions);
				vec_foreach(it->options, oi)
					free(it->options.d[oi].opt);
				vec_free(&it->options);
			}
			vec_free(&cur_formula.items);
			pkg_deps_formula_free(res);

			return (NULL);

			break;
		}
	}

	if (state != st_skip_spaces && state != st_parse_comma) {
		pkg_emit_error("cannot parse pkg formula: %s", in);
		free(cur_item.name);
		vec_foreach(cur_item.versions, vi)
			free(cur_item.versions.d[vi].ver);
		vec_free(&cur_item.versions);
		vec_foreach(cur_item.options, oi)
			free(cur_item.options.d[oi].opt);
		vec_free(&cur_item.options);
		vec_foreach(cur_formula.items, ii) {
			struct pkg_dep_formula_item *it = &cur_formula.items.d[ii];
			free(it->name);
			vec_foreach(it->versions, vi)
				free(it->versions.d[vi].ver);
			vec_free(&it->versions);
			vec_foreach(it->options, oi)
				free(it->options.d[oi].opt);
			vec_free(&it->options);
		}
		vec_free(&cur_formula.items);
		pkg_deps_formula_free(res);

		return (NULL);
	}

	return (res);
}

void
pkg_deps_formula_free(dep_formulav_t *f)
{
	if (f == NULL)
		return;

	vec_foreach(*f, fi) {
		vec_foreach(f->d[fi].items, ii) {
			struct pkg_dep_formula_item *it = &f->d[fi].items.d[ii];
			free(it->name);
			vec_foreach(it->versions, vi)
				free(it->versions.d[vi].ver);
			vec_free(&it->versions);
			vec_foreach(it->options, oi)
				free(it->options.d[oi].opt);
			vec_free(&it->options);
		}
		vec_free(&f->d[fi].items);
	}
	vec_free(f);
	free(f);
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
pkg_deps_formula_tostring(dep_formulav_t *f)
{
	char *res = NULL, *p;

	int rlen = 0, r;

	vec_foreach(*f, fi) {
		vec_foreach(f->d[fi].items, ii) {
			struct pkg_dep_formula_item *cit = &f->d[fi].items.d[ii];
			rlen += strlen(cit->name);

			vec_foreach(cit->versions, vi) {
				rlen += strlen(cit->versions.d[vi].ver);
				rlen += 4; /* <OP><SP><VER><SP> */
			}

			vec_foreach(cit->options, oi) {
				rlen += strlen(cit->options.d[oi].opt);
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

	vec_foreach(*f, fi) {
		vec_foreach(f->d[fi].items, ii) {
			struct pkg_dep_formula_item *cit = &f->d[fi].items.d[ii];
			r = snprintf(p, rlen, "%s", cit->name);
			p += r;
			rlen -= r;

			vec_foreach(cit->versions, vi) {
				r = snprintf(p, rlen, " %s %s",
						pkg_deps_op_tostring(cit->versions.d[vi].op),
						cit->versions.d[vi].ver);
				p += r;
				rlen -= r;
			}

			vec_foreach(cit->options, oi) {
				r = snprintf(p, rlen, " %c%s",
						cit->options.d[oi].on ? '+' : '-',
						cit->options.d[oi].opt);
				p += r;
				rlen -= r;
			}

			r = snprintf(p, rlen, "%s",
					ii + 1 < f->d[fi].items.len ? " | " : "");
			p += r;
			rlen -= r;
		}

		r = snprintf(p, rlen, "%s", fi + 1 < f->len ? ", " : "");
		p += r;
		rlen -= r;
	}

	return (res);
}

char*
pkg_deps_formula_tosql(dep_itemv_t *f)
{
	struct pkg_dep_formula_item *cit;
	char *res = NULL, *p;

	int rlen = 0, r;

	vec_foreach(*f, ii) {
		cit = &f->d[ii];
		rlen += sizeof("AND (name='' )");
		rlen += strlen(cit->name);

		vec_foreach(cit->versions, vi) {
			rlen += sizeof(" AND vercmp(>=, version,'') ");
			rlen += strlen(cit->versions.d[vi].ver);
		}

		rlen += sizeof(" OR ");
	}

	if (rlen == 0) {
		return (NULL);
	}

	res = xmalloc(rlen + 1);

	p = res;

	vec_foreach(*f, ii) {
		cit = &f->d[ii];
		r = snprintf(p, rlen, "(name='%s'", cit->name);
		p += r;
		rlen -= r;

		vec_foreach(cit->versions, vi) {
			r = snprintf(p, rlen, " AND vercmp('%s',version,'%s')",
					pkg_deps_op_tostring(cit->versions.d[vi].op),
					cit->versions.d[vi].ver);
			p += r;
			rlen -= r;
		}
		r = snprintf(p, rlen, ")%s", ii + 1 < f->len ? " OR " : "");
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
