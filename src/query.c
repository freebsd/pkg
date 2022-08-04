/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
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

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>
#include "pkgcli.h"

static struct query_flags accepted_query_flags[] = {
	{ 'd', "nov",		1, PKG_LOAD_DEPS },
	{ 'r', "nov",		1, PKG_LOAD_RDEPS },
	{ 'C', "",		1, PKG_LOAD_CATEGORIES },
	{ 'F', "ps",		1, PKG_LOAD_FILES },
	{ 'O', "kvdD",		1, PKG_LOAD_OPTIONS },
	{ 'D', "",		1, PKG_LOAD_DIRS },
	{ 'L', "",		1, PKG_LOAD_LICENSES },
	{ 'U', "",		1, PKG_LOAD_USERS },
	{ 'G', "",		1, PKG_LOAD_GROUPS },
	{ 'B', "",		1, PKG_LOAD_SHLIBS_REQUIRED },
	{ 'b', "",		1, PKG_LOAD_SHLIBS_PROVIDED },
	{ 'A', "tv",            1, PKG_LOAD_ANNOTATIONS },
	{ '?', "drCFODLUGBbA",	1, PKG_LOAD_BASIC },	/* dbflags handled in analyse_query_string() */
	{ '#', "drCFODLUGBbA",	1, PKG_LOAD_BASIC },	/* dbflags handled in analyse_query_string() */
	{ 's', "hb",		0, PKG_LOAD_BASIC },
	{ 'Q', "",		0, PKG_LOAD_BASIC },
	{ 'n', "",		0, PKG_LOAD_BASIC },
	{ 'v', "",		0, PKG_LOAD_BASIC },
	{ 'o', "",		0, PKG_LOAD_BASIC },
	{ 'p', "",		0, PKG_LOAD_BASIC },
	{ 'm', "",		0, PKG_LOAD_BASIC },
	{ 'c', "",		0, PKG_LOAD_BASIC },
	{ 'e', "",		0, PKG_LOAD_BASIC },
	{ 'w', "",		0, PKG_LOAD_BASIC },
	{ 'l', "",		0, PKG_LOAD_BASIC },
	{ 'q', "",		0, PKG_LOAD_BASIC },
	{ 'a', "",		0, PKG_LOAD_BASIC },
	{ 'k', "",		0, PKG_LOAD_BASIC },
	{ 'M', "",		0, PKG_LOAD_BASIC },
	{ 't', "",		0, PKG_LOAD_BASIC },
	{ 'R', "",              0, PKG_LOAD_ANNOTATIONS },
	{ 'V', "",		0, PKG_LOAD_BASIC },
	{ 'X', "",		0, PKG_LOAD_BASIC | PKG_LOAD_SCRIPTS | PKG_LOAD_LUA_SCRIPTS },
};

static void
format_str(struct pkg *pkg, xstring *dest, const char *qstr, const void *data)
{
	bool automatic;
	bool locked;
	bool vital;

	xstring_reset(dest);

	while (qstr[0] != '\0') {
		if (qstr[0] == '%') {
			qstr++;
			switch (qstr[0]) {
			case 'n':
				pkg_fprintf(dest->fp, "%n", pkg);
				break;
			case 'v':
				pkg_fprintf(dest->fp, "%v", pkg);
				break;
			case 'o':
				pkg_fprintf(dest->fp, "%o", pkg);
				break;
			case 'R':
				pkg_fprintf(dest->fp, "%N", pkg);
				break;
			case 'p':
				pkg_fprintf(dest->fp, "%p", pkg);
				break;
			case 'm':
				pkg_fprintf(dest->fp, "%m", pkg);
				break;
			case 'c':
				pkg_fprintf(dest->fp, "%c", pkg);
				break;
			case 'w':
				pkg_fprintf(dest->fp, "%w", pkg);
				break;
			case 'a':
				pkg_get_bool(pkg, PKG_AUTOMATIC, automatic);
				fprintf(dest->fp, "%d", automatic);
				break;
			case 'k':
				pkg_get_bool(pkg, PKG_LOCKED, locked);
				fprintf(dest->fp, "%d", locked);
				break;
			case 't':
				pkg_fprintf(dest->fp, "%t", pkg);
				break;
			case 's':
				qstr++;
				if (qstr[0] == 'h')
					pkg_fprintf(dest->fp, "%#sB", pkg);
				else if (qstr[0] == 'b')
					pkg_fprintf(dest->fp, "%s", pkg);
				break;
			case 'e':
				pkg_fprintf(dest->fp, "%e", pkg);
				break;
			case '?':
				qstr++;
				switch (qstr[0]) {
				case 'd':
					pkg_fprintf(dest->fp, "%?d", pkg);
					break;
				case 'r':
					pkg_fprintf(dest->fp, "%?r", pkg);
					break;
				case 'C':
					pkg_fprintf(dest->fp, "%?C", pkg);
					break;
				case 'F':
					pkg_fprintf(dest->fp, "%?F", pkg);
					break;
				case 'O':
					pkg_fprintf(dest->fp, "%?O", pkg);
					break;
				case 'D':
					pkg_fprintf(dest->fp, "%?D", pkg);
					break;
				case 'L':
					pkg_fprintf(dest->fp, "%?L", pkg);
					break;
				case 'U':
					pkg_fprintf(dest->fp, "%?U", pkg);
					break;
				case 'G':
					pkg_fprintf(dest->fp, "%?G", pkg);
					break;
				case 'B':
					pkg_fprintf(dest->fp, "%?B", pkg);
					break;
				case 'b':
					pkg_fprintf(dest->fp, "%?b", pkg);
					break;
				case 'A':
					pkg_fprintf(dest->fp, "%?A", pkg);
					break;
				}
				break;
			case '#':
				qstr++;
				switch (qstr[0]) {
				case 'd':
					pkg_fprintf(dest->fp, "%#d", pkg);
					break;
				case 'r':
					pkg_fprintf(dest->fp, "%#r", pkg);
					break;
				case 'C':
					pkg_fprintf(dest->fp, "%#C", pkg);
					break;
				case 'F':
					pkg_fprintf(dest->fp, "%#F", pkg);
					break;
				case 'O':
					pkg_fprintf(dest->fp, "%#O", pkg);
					break;
				case 'D':
					pkg_fprintf(dest->fp, "%#D", pkg);
					break;
				case 'L':
					pkg_fprintf(dest->fp, "%#L", pkg);
					break;
				case 'U':
					pkg_fprintf(dest->fp, "%#U", pkg);
					break;
				case 'G':
					pkg_fprintf(dest->fp, "%#G", pkg);
					break;
				case 'B':
					pkg_fprintf(dest->fp, "%#B", pkg);
					break;
				case 'b':
					pkg_fprintf(dest->fp, "%#b", pkg);
					break;
				case 'A':
					pkg_fprintf(dest->fp, "%#A", pkg);
					break;
				}
				break;
			case 'Q':
				pkg_fprintf(dest->fp, "%Q", pkg);
				break;
			case 'q':
				pkg_fprintf(dest->fp, "%q", pkg);
				break;
			case 'l':
				pkg_fprintf(dest->fp, "%l", pkg);
				break;
			case 'd':
				qstr++;
				if (qstr[0] == 'n')
					pkg_fprintf(dest->fp, "%dn", data);
				else if (qstr[0] == 'o')
					pkg_fprintf(dest->fp, "%do", data);
				else if (qstr[0] == 'v')
					pkg_fprintf(dest->fp, "%dv", data);
				break;
			case 'r':
				qstr++;
				if (qstr[0] == 'n')
					pkg_fprintf(dest->fp, "%rn", data);
				else if (qstr[0] == 'o')
					pkg_fprintf(dest->fp, "%ro", data);
				else if (qstr[0] == 'v')
					pkg_fprintf(dest->fp, "%rv", data);
				break;
			case 'C':
				pkg_fprintf(dest->fp, "%Cn", data);
				break;
			case 'F':
				qstr++;
				if (qstr[0] == 'p')
					pkg_fprintf(dest->fp, "%Fn", data);
				else if (qstr[0] == 's')
					pkg_fprintf(dest->fp, "%Fs", data);
				break;
			case 'O':
				qstr++;
				if (qstr[0] == 'k')
					pkg_fprintf(dest->fp, "%On", data);
				else if (qstr[0] == 'v')
					pkg_fprintf(dest->fp, "%Ov", data);
				else if (qstr[0] == 'd') /* default value */
					pkg_fprintf(dest->fp, "%Od", data);
				else if (qstr[0] == 'D') /* description */
					pkg_fprintf(dest->fp, "%OD", data);
				break;
			case 'D':
				pkg_fprintf(dest->fp, "%Dn", data);
				break;
			case 'L':
				pkg_fprintf(dest->fp, "%Ln", data);
				break;
			case 'U':
				pkg_fprintf(dest->fp, "%Un", data);
				break;
			case 'G':
				pkg_fprintf(dest->fp, "%Gn", data);
				break;
			case 'B':
				pkg_fprintf(dest->fp, "%Bn", data);
				break;
			case 'b':
				pkg_fprintf(dest->fp, "%bn", data);
				break;
			case 'A':
				qstr++;
				if (qstr[0] == 't')
					pkg_fprintf(dest->fp, "%An", data);
				else if (qstr[0] == 'v')
					pkg_fprintf(dest->fp, "%Av", data);
				break;
			case 'M':
				if (pkg_has_message(pkg))
					pkg_fprintf(dest->fp, "%M", pkg);
				break;
			case 'V':
				pkg_get_bool(pkg, PKG_VITAL, vital);
				fprintf(dest->fp, "%d", vital);
				break;
			case 'X':
				pkg_fprintf(dest->fp, "%X", pkg);
				break;
			case '%':
				fprintf(dest->fp, "%c", '%');
				break;
			}
		} else  if (qstr[0] == '\\') {
			qstr++;
			switch (qstr[0]) {
			case 'n':
				fprintf(dest->fp, "%c", '\n');
				break;
			case 'a':
				fprintf(dest->fp, "%c", '\a');
				break;
			case 'b':
				fprintf(dest->fp, "%c", '\b');
				break;
			case 'f':
				fprintf(dest->fp, "%c", '\f');
				break;
			case 'r':
				fprintf(dest->fp, "%c", '\r');
				break;
			case '\\':
				fprintf(dest->fp, "%c", '\\');
				break;
			case 't':
				fprintf(dest->fp, "%c", '\t');
				break;
			}
		} else {
			fprintf(dest->fp, "%c", qstr[0]);
		}
		qstr++;
	}
	fflush(dest->fp);
}

void
print_query(struct pkg *pkg, char *qstr, char multiline)
{
	xstring			*output;
	struct pkg_dep		*dep    = NULL;
	struct pkg_option	*option = NULL;
	struct pkg_file		*file   = NULL;
	struct pkg_dir		*dir    = NULL;
	const char		*str;
	struct pkg_kv		*kv;
	struct pkg_stringlist	*sl;
	struct pkg_stringlist_iterator	*slit;
	struct pkg_kvlist	*kl;
	struct pkg_kvlist_iterator	*kit;

	output = xstring_new();

	switch (multiline) {
	case 'd':
		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			format_str(pkg, output, qstr, dep);
			printf("%s\n", output->buf);
		}
		break;
	case 'r':
		while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
			format_str(pkg, output, qstr, dep);
			printf("%s\n", output->buf);
		}
		break;
	case 'C':
		pkg_get_stringlist(pkg, PKG_CATEGORIES, sl);
		slit = pkg_stringlist_iterator(sl);
		while ((str = pkg_stringlist_next(slit))) {
			format_str(pkg, output, qstr, str);
			printf("%s\n", output->buf);
		}
		free(slit);
		free(sl);
		break;
	case 'O':
		while (pkg_options(pkg, &option) == EPKG_OK) {
			format_str(pkg, output, qstr, option);
			printf("%s\n", output->buf);
		}
		break;
	case 'F':
		while (pkg_files(pkg, &file) == EPKG_OK) {
			format_str(pkg, output, qstr, file);
			printf("%s\n", output->buf);
		}
		break;
	case 'D':
		while (pkg_dirs(pkg, &dir) == EPKG_OK) {
			format_str(pkg, output, qstr, dir);
			printf("%s\n", output->buf);
		}
		break;
	case 'L':
		pkg_get_stringlist(pkg, PKG_LICENSES, sl);
		slit = pkg_stringlist_iterator(sl);
		while ((str = pkg_stringlist_next(slit))) {
			format_str(pkg, output, qstr, str);
			printf("%s\n", output->buf);
		}
		free(slit);
		free(sl);
		break;
	case 'U':
		pkg_get_stringlist(pkg, PKG_USERS, sl);
		slit = pkg_stringlist_iterator(sl);
		while ((str = pkg_stringlist_next(slit))) {
			format_str(pkg, output, qstr, str);
			printf("%s\n", output->buf);
		}
		break;
	case 'G':
		pkg_get_stringlist(pkg, PKG_GROUPS, sl);
		slit = pkg_stringlist_iterator(sl);
		while ((str = pkg_stringlist_next(slit))) {
			format_str(pkg, output, qstr, str);
			printf("%s\n", output->buf);
		}
		break;
	case 'B':
		pkg_get_stringlist(pkg, PKG_SHLIBS_REQUIRED, sl);
		slit = pkg_stringlist_iterator(sl);
		while ((str = pkg_stringlist_next(slit))) {
			format_str(pkg, output, qstr, str);
			printf("%s\n", output->buf);
		}
		break;
	case 'b':
		pkg_get_stringlist(pkg, PKG_SHLIBS_PROVIDED, sl);
		slit = pkg_stringlist_iterator(sl);
		while ((str = pkg_stringlist_next(slit))) {
			format_str(pkg, output, qstr, str);
			printf("%s\n", output->buf);
		}
		break;
	case 'A':
		pkg_get_kv(pkg, PKG_ANNOTATIONS, kl);
		kit = pkg_kvlist_iterator(kl);
		while ((kv = pkg_kvlist_next(kit))) {
			format_str(pkg, output, qstr, kv);
			printf("%s\n", output->buf);
		}
		free(kit);
		free(kl);
		break;
	default:
		format_str(pkg, output, qstr, dep);
		printf("%s\n", output->buf);
		break;
	}
	xstring_free(output);
}

typedef enum {
	NONE,
	NEXT_IS_INT,
	OPERATOR_INT,
	INT,
	NEXT_IS_STRING,
	OPERATOR_STRING,
	STRING,
	QUOTEDSTRING,
	SQUOTEDSTRING,
	POST_EXPR,
} state_t;

int
format_sql_condition(const char *str, xstring *sqlcond, bool for_remote)
{
	state_t state = NONE;
	unsigned int bracket_level = 0;
	const char *sqlop;

	fprintf(sqlcond->fp, " WHERE ");
	while (str[0] != '\0') {
		if (state == NONE) {
			if (str[0] == '%') {
				str++;
				switch (str[0]) {
				case 'n':
					fprintf(sqlcond->fp, "p.name");
					state = OPERATOR_STRING;
					break;
				case 'o':
					fprintf(sqlcond->fp, "origin");
					state = OPERATOR_STRING;
					break;
				case 'p':
					fprintf(sqlcond->fp, "prefix");
					state = OPERATOR_STRING;
					break;
				case 'm':
					fprintf(sqlcond->fp, "maintainer");
					state = OPERATOR_STRING;
					break;
				case 'c':
					fprintf(sqlcond->fp, "comment");
					state = OPERATOR_STRING;
					break;
				case 'w':
					fprintf(sqlcond->fp, "www");
					state = OPERATOR_STRING;
					break;
				case 's':
					fprintf(sqlcond->fp, "flatsize");
					state = OPERATOR_INT;
					break;
				case 'a':
					if (for_remote)
						goto bad_option;
					fprintf(sqlcond->fp, "automatic");
					state = OPERATOR_INT;
					break;
				case 'q':
					fprintf(sqlcond->fp, "arch");
					state = OPERATOR_STRING;
					break;
				case 'k':
					if (for_remote)
						goto bad_option;
					fprintf(sqlcond->fp, "locked");
					state = OPERATOR_INT;
					break;
				case 'M':
					if (for_remote)
						goto bad_option;
					fprintf(sqlcond->fp, "message");
					state = OPERATOR_STRING;
					break;
				case 't':
					if (for_remote)
						goto bad_option;
					fprintf(sqlcond->fp, "time");
					state = OPERATOR_INT;
					break;
				case 'e':
					fprintf(sqlcond->fp, "desc");
					state = OPERATOR_STRING;
					break;
				case 'V':
					if (for_remote)
						goto bad_option;
					fprintf(sqlcond->fp, "vital");
					state = OPERATOR_INT;
					break;
				case '#': /* FALLTHROUGH */
				case '?':
					sqlop = (str[0] == '#' ? "COUNT(*)" : "COUNT(*) > 0");
					str++;
					switch (str[0]) {
						case 'd':
							fprintf(sqlcond->fp, "(SELECT %s FROM deps AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'r':
							fprintf(sqlcond->fp, "(SELECT %s FROM deps AS d WHERE d.name=p.name)", sqlop);
							break;
						case 'C':
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_categories AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'F':
							if (for_remote)
								goto bad_option;
							fprintf(sqlcond->fp, "(SELECT %s FROM files AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'O':
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_option AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'D':
							if (for_remote)
								goto bad_option;
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_directories AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'L':
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_licenses AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'U':
							if (for_remote)
								goto bad_option;
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_users AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'G':
							if (for_remote)
								goto bad_option;
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_groups AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'B':
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_shlibs_required AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'b':
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_shlibs_provided AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'A':
							fprintf(sqlcond->fp, "(SELECT %s FROM pkg_annotation AS d WHERE d.package_id=p.id)", sqlop);
							break;
						default:
							goto bad_option;
					}
					state = OPERATOR_INT;
					break;
				default:
bad_option:
					fprintf(stderr, "malformed evaluation string\n");
					return (EPKG_FATAL);
				}
			} else {
				switch (str[0]) {
				case '(':
					bracket_level++;
					fprintf(sqlcond->fp, "%c", str[0]);
					break;
				case ' ':
				case '\t':
					break;
				default:
					fprintf(stderr, "unexpected character: %c\n", str[0]);
					return (EPKG_FATAL);
				}
			}
		} else if (state == POST_EXPR) {
			switch (str[0]) {
			case ')':
				if (bracket_level == 0) {
					fprintf(stderr, "too many closing brackets.\n");
					return (EPKG_FATAL);
				}
				bracket_level--;
				fprintf(sqlcond->fp, "%c", str[0]);
				break;
			case ' ':
			case '\t':
				break;
			case '|':
				if (str[1] == '|') {
					str++;
					state = NONE;
					fprintf(sqlcond->fp, " OR ");
					break;
				} else {
					fprintf(stderr, "unexpected character %c\n", str[1]);
					return (EPKG_FATAL);
				}
			case '&':
				if (str[1] == '&') {
					str++;
					state = NONE;
					fprintf(sqlcond->fp, " AND ");
					break;
				} else {
					fprintf(stderr, "unexpected character %c\n", str[1]);
					return (EPKG_FATAL);
				}
			default:
				fprintf(stderr, "unexpected character %c\n", str[0]);
				return (EPKG_FATAL);
			}
		} else if (state == OPERATOR_STRING || state == OPERATOR_INT) {
			/* only operators or space are allowed here */
			if (isspace(str[0])) {
				/* do nothing */
			} else if (str[0] == '~' ) {
				if (state != OPERATOR_STRING) {
					fprintf(stderr, "~ expected only for string testing\n");
					return (EPKG_FATAL);
				}
				state = NEXT_IS_STRING;
				fprintf(sqlcond->fp, " GLOB ");
			} else if (str[0] == '>' || str[0] == '<') {
				if (state != OPERATOR_INT) {
					fprintf(stderr, "> expected only for integers\n");
					return (EPKG_FATAL);
				}
				state = NEXT_IS_INT;
				fprintf(sqlcond->fp, "%c", str[0]);
				if (str[1] == '=') {
					str++;
					fprintf(sqlcond->fp, "%c", str[0]);
				}
			} else if (str[0] == '=') {
				if (state == OPERATOR_STRING) {
					state = NEXT_IS_STRING;
				} else {
					state = NEXT_IS_INT;
				}
				fprintf(sqlcond->fp, "%c", str[0]);
				if (str[1] == '=') {
					str++;
					fprintf(sqlcond->fp, "%c", str[0]);
				}
			} else if (str[0] == '!') {
				if (str[1] == '=') {
					fprintf(sqlcond->fp, "%c", str[0]);
					fprintf(sqlcond->fp, "%c", str[1]);
				} else if (str[1] == '~') {
					fprintf(sqlcond->fp, " NOT GLOB ");
				} else {
					fprintf(stderr, "expecting = or ~ after !\n");
					return (EPKG_FATAL);
				}
				str++;
				if (state == OPERATOR_STRING) {
					state = NEXT_IS_STRING;
				} else {
					state = NEXT_IS_INT;
				}
			} else {
				fprintf(stderr, "an operator is expected, got %c\n", str[0]);
				return (EPKG_FATAL);
			}
		} else if (state == NEXT_IS_STRING || state == NEXT_IS_INT) {
			if (isspace(str[0])) {
				/* do nothing */
			} else {
				if (state == NEXT_IS_STRING) {
					if (str[0] == '"') {
						state = QUOTEDSTRING;
					} else if (str[0] == '\'') {
						state = SQUOTEDSTRING;
					} else {
						state = STRING;
						str--;
					}
					fprintf(sqlcond->fp, "%c", '\'');
				} else {
					if (!isdigit(str[0])) {
						fprintf(stderr, "a number is expected, got: %c\n", str[0]);
						return (EPKG_FATAL);
					}
					state = INT;
					fprintf(sqlcond->fp, "%c", str[0]);
				}
			}
		} else if (state == INT) {
			if (!isdigit(str[0])) {
				state = POST_EXPR;
				str--;
			} else {
				fprintf(sqlcond->fp, "%c", str[0]);
			}
		} else if (state == STRING || state == QUOTEDSTRING || state == SQUOTEDSTRING) {
			if ((state == STRING && isspace(str[0])) ||
			    (state == QUOTEDSTRING && str[0] == '"') ||
			    (state == SQUOTEDSTRING && str[0] == '\'')) {
				fprintf(sqlcond->fp, "%c", '\'');
				state = POST_EXPR;
			} else {
				fprintf(sqlcond->fp, "%c", str[0]);
				if (str[0] == '\'')
					fprintf(sqlcond->fp, "%c", str[0]);
				else if (str[0] == '%' && for_remote)
					fprintf(sqlcond->fp, "%c", str[0]);
			}
		}
		str++;
	}
	if (state == STRING) {
		fprintf(sqlcond->fp, "%c", '\'');
		state = POST_EXPR;
	}

	if (state != POST_EXPR && state != INT) {
		fprintf(stderr, "unexpected end of expression\n");
		return (EPKG_FATAL);
	} else if (bracket_level > 0) {
		fprintf(stderr, "unexpected end of expression (too many open brackets)\n");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
analyse_query_string(char *qstr, struct query_flags *q_flags, const unsigned int q_flags_len, int *flags, char *multiline)
{
	unsigned int i, j, k;
	unsigned int valid_flag = 0;
	unsigned int valid_opts = 0;

	j = 0; /* shut up scanbuild */

	if (strchr(qstr, '%') == NULL) {
		fprintf(stderr, "Invalid query: query should contain a format string\n");
		return (EPKG_FATAL);
	}

	while (qstr[0] != '\0') {
		if (qstr[0] == '%') {
			qstr++;
			valid_flag = 0;

			for (i = 0; i < q_flags_len; i++) {
				/* found the flag */
				if (qstr[0] == q_flags[i].flag) {
					valid_flag = 1;

					/* if the flag is followed by additional options */
					if (q_flags[i].options[0] != '\0') {
						qstr++;
						valid_opts = 0;

						for (j = 0; j < strlen(q_flags[i].options); j++) {
							if (qstr[0] == q_flags[i].options[j]) {
								valid_opts = 1;
								break;
							}
						}

						if (valid_opts == 0) {
							fprintf(stderr, "Invalid query: '%%%c' should be followed by:", q_flags[i].flag);

							for (j = 0; j < strlen(q_flags[i].options); j++)
								fprintf(stderr, " %c%c", q_flags[i].options[j],
										q_flags[i].options[j + 1] == '\0' ?
										'\n' : ',');

							return (EPKG_FATAL);
						}
					}

					/* if this is a multiline flag */
					if (q_flags[i].multiline == 1) {
						if (*multiline != 0 && *multiline != q_flags[i].flag) {
							fprintf(stderr, "Invalid query: '%%%c' and '%%%c' cannot be queried at the same time\n",
									*multiline, q_flags[i].flag);
							return (EPKG_FATAL);
						} else {
							*multiline = q_flags[i].flag;
						}
					}

					/* handle the '?' flag cases */
					if (q_flags[i].flag == '?' || q_flags[i].flag == '#') {
						for (k = 0; k < q_flags_len; k++)
							if (q_flags[k].flag == q_flags[i].options[j]) {
								*flags |= q_flags[k].dbflags;
								break;
							}
					} else {
						*flags |= q_flags[i].dbflags;
					}

					break; /* don't iterate over the rest of the flags */
				}
			}

			if (valid_flag == 0) {
				fprintf(stderr, "Unknown query format key: '%%%c'\n", qstr[0]);
				return (EPKG_FATAL);
			}
		}

		qstr++;
	}

	return (EPKG_OK);
}

void
usage_query(void)
{
	fprintf(stderr, "Usage: pkg query <query-format> <pkg-name>\n");
	fprintf(stderr, "       pkg query [-a] <query-format>\n");
	fprintf(stderr, "       pkg query -F <pkg-name> <query-format>\n");
	fprintf(stderr, "       pkg query -e <evaluation> <query-format>\n");
	fprintf(stderr, "       pkg query [-Cgix] <query-format> <pattern> <...>\n\n");
	fprintf(stderr, "For more information see 'pkg help query.'\n");
}

int
exec_query(int argc, char **argv)
{
	struct pkgdb		*db = NULL;
	struct pkgdb_it		*it = NULL;
	struct pkg		*pkg = NULL;
	struct pkg_manifest_key	*keys = NULL;
	char			*pkgname = NULL;
	int			 query_flags = PKG_LOAD_BASIC;
	match_t			 match = MATCH_EXACT;
	int			 ch;
	int			 ret;
	int			 retcode = EXIT_SUCCESS;
	int			 i;
	char			 multiline = 0;
	int			 nprinted = 0;
	char			*condition = NULL;
	const char 		*condition_sql = NULL;
	xstring			*sqlcond = NULL;
	const unsigned int	 q_flags_len = NELEM(accepted_query_flags);

	struct option longopts[] = {
		{ "all",		no_argument,		NULL,	'a' },
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "evaluate",		required_argument,	NULL,	'e' },
		{ "file",		required_argument,	NULL,	'F' },
		{ "glob",		no_argument,		NULL,	'g' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "regex",		no_argument,		NULL,	'x' },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+aCe:F:gix", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'e':
			condition = optarg;
			break;
		case 'F':
			pkgname = optarg;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		default:
			usage_query();
			return (EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		usage_query();
		return (EXIT_FAILURE);
	}

	/* Default to all packages if no pkg provided */
	if (argc == 1 && pkgname == NULL && match == MATCH_EXACT) {
		match = MATCH_ALL;
	} else if (((argc == 1) ^ (match == MATCH_ALL)) && pkgname == NULL
			&& condition == NULL) {
		usage_query();
		return (EXIT_FAILURE);
	}

	if (analyse_query_string(argv[0], accepted_query_flags, q_flags_len,
			&query_flags, &multiline) != EPKG_OK)
		return (EXIT_FAILURE);

	if (pkgname != NULL) {
		/* Use a manifest or compact manifest if possible. */
		int open_flags = 0;
		if ((query_flags & ~(PKG_LOAD_DEPS|
				     PKG_LOAD_OPTIONS|
				     PKG_LOAD_CATEGORIES|
				     PKG_LOAD_LICENSES|
				     PKG_LOAD_USERS|
				     PKG_LOAD_GROUPS|
				     PKG_LOAD_SHLIBS_REQUIRED|
				     PKG_LOAD_SHLIBS_PROVIDED|
				     PKG_LOAD_ANNOTATIONS|
				     PKG_LOAD_CONFLICTS|
				     PKG_LOAD_PROVIDES|
				     PKG_LOAD_REQUIRES)) == 0) {
			open_flags = PKG_OPEN_MANIFEST_COMPACT;
		} else if ((query_flags & PKG_LOAD_FILES) == 0) {
			open_flags = PKG_OPEN_MANIFEST_ONLY;
		}
		pkg_manifest_keys_new(&keys);
		if (pkg_open(&pkg, pkgname, keys, open_flags) != EPKG_OK) {
			return (EXIT_FAILURE);
		}

		pkg_manifest_keys_free(keys);
		print_query(pkg, argv[0], multiline);
		pkg_free(pkg);
		return (EXIT_SUCCESS);
	}

	if (condition != NULL) {
		sqlcond = xstring_new();
		if (format_sql_condition(condition, sqlcond, false) != EPKG_OK) {
			xstring_free(sqlcond);
			return (EXIT_FAILURE);
		}
	}

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to query the package database");
		return (EXIT_FAILURE);
	} else if (ret == EPKG_ENODB) {
		if (!quiet)
			warnx("No packages installed");
		return (EXIT_SUCCESS);
	} else if (ret != EPKG_OK)
		return (EXIT_FAILURE);

	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret != EPKG_OK)
		return (EXIT_FAILURE);

	drop_privileges();
	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EXIT_FAILURE);
	}

	if (sqlcond) {
		fflush(sqlcond->fp);
		condition_sql = sqlcond->buf;
	}
        i = 1;
        do {
		pkgname = i < argc ? argv[i] : NULL;

		if ((it = pkgdb_query_cond(db, condition_sql, pkgname, match)) == NULL) {
			warnx("DEBUG: %s/%s\n", condition_sql ? condition_sql : "-", pkgname ? pkgname : "-");
			retcode = EXIT_FAILURE;
			break;
		}

		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
			nprinted++;
			print_query(pkg, argv[0], multiline);
		}

		if (ret != EPKG_END) {
			retcode = EXIT_FAILURE;
			break;
		}

		pkgdb_it_free(it);
		i++;
	} while (i < argc);

	if (nprinted == 0 && match != MATCH_ALL && retcode == EXIT_SUCCESS) {
		/* ensure to return a non-zero status when no package
		 were found. */
		retcode = EXIT_FAILURE;
	}

	pkg_free(pkg);

	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (retcode);
}
