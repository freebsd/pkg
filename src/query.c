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
#include <sys/sbuf.h>

#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <inttypes.h>
#include <pkg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

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
};

static void
format_str(struct pkg *pkg, struct sbuf *dest, const char *qstr, const void *data)
{
	bool automatic;
	bool locked;

	sbuf_clear(dest);

	while (qstr[0] != '\0') {
		if (qstr[0] == '%') {
			qstr++;
			switch (qstr[0]) {
			case 'n':
				pkg_sbuf_printf(dest, "%n", pkg);
				break;
			case 'v':
				pkg_sbuf_printf(dest, "%v", pkg);
				break;
			case 'o':
				pkg_sbuf_printf(dest, "%o", pkg);
				break;
			case 'R':
				pkg_sbuf_printf(dest, "%N", pkg);
				break;
			case 'p':
				pkg_sbuf_printf(dest, "%p", pkg);
				break;
			case 'm':
				pkg_sbuf_printf(dest, "%m", pkg);
				break;
			case 'c':
				pkg_sbuf_printf(dest, "%c", pkg);
				break;
			case 'w':
				pkg_sbuf_printf(dest, "%w", pkg);
				break;
			case 'a':
				pkg_get(pkg, PKG_AUTOMATIC, &automatic);
				sbuf_printf(dest, "%d", automatic);
				break;
			case 'k':
				pkg_get(pkg, PKG_LOCKED, &locked);
				sbuf_printf(dest, "%d", locked);
				break;
			case 't':
				pkg_sbuf_printf(dest, "%t", pkg);
				break;
			case 's':
				qstr++;
				if (qstr[0] == 'h') 
					pkg_sbuf_printf(dest, "%?sB", pkg);
			        else if (qstr[0] == 'b')
					pkg_sbuf_printf(dest, "%s", pkg);
				break;
			case 'e':
				pkg_sbuf_printf(dest, "%e", pkg);
				break;
			case '?':
				qstr++;
				switch (qstr[0]) {
				case 'd':
					pkg_sbuf_printf(dest, "%?d", pkg);
					break;
				case 'r':
					pkg_sbuf_printf(dest, "%?r", pkg);
					break;
				case 'C':
					pkg_sbuf_printf(dest, "%?C", pkg);
					break;
				case 'F':
					pkg_sbuf_printf(dest, "%?F", pkg);
					break;
				case 'O':
					pkg_sbuf_printf(dest, "%?O", pkg);
					break;
				case 'D':
					pkg_sbuf_printf(dest, "%?D", pkg);
					break;
				case 'L':
					pkg_sbuf_printf(dest, "%?L", pkg);
					break;
				case 'U':
					pkg_sbuf_printf(dest, "%?U", pkg);
					break;
				case 'G':
					pkg_sbuf_printf(dest, "%?G", pkg);
					break;
				case 'B':
					pkg_sbuf_printf(dest, "%?B", pkg);
					break;
				case 'b':
					pkg_sbuf_printf(dest, "%?b", pkg);
					break;
				case 'A':
					pkg_sbuf_printf(dest, "%?A", pkg);
					break;
				}
				break;
			case '#':
				qstr++;
				switch (qstr[0]) {
				case 'd':
					pkg_sbuf_printf(dest, "%#d", pkg);
					break;
				case 'r':
					pkg_sbuf_printf(dest, "%#r", pkg);
					break;
				case 'C':
					pkg_sbuf_printf(dest, "%#C", pkg);
					break;
				case 'F':
					pkg_sbuf_printf(dest, "%#F", pkg);
					break;
				case 'O':
					pkg_sbuf_printf(dest, "%#O", pkg);
					break;
				case 'D':
					pkg_sbuf_printf(dest, "%#D", pkg);
					break;
				case 'L':
					pkg_sbuf_printf(dest, "%#L", pkg);
					break;
				case 'U':
					pkg_sbuf_printf(dest, "%#U", pkg);
					break;
				case 'G':
					pkg_sbuf_printf(dest, "%#G", pkg);
					break;
				case 'B':
					pkg_sbuf_printf(dest, "%#B", pkg);
					break;
				case 'b':
					pkg_sbuf_printf(dest, "%#b", pkg);
					break;
				case 'A':
					pkg_sbuf_printf(dest, "%#A", pkg);
					break;
				}
				break;
			case 'q':
				pkg_sbuf_printf(dest, "%q", pkg);
				break;
			case 'l':
				pkg_sbuf_printf(dest, "%l", pkg);
				break;
			case 'd':
				qstr++;
				if (qstr[0] == 'n')
					pkg_sbuf_printf(dest, "%dn", data);
				else if (qstr[0] == 'o')
					pkg_sbuf_printf(dest, "%do", data);
				else if (qstr[0] == 'v')
					pkg_sbuf_printf(dest, "%dv", data);
				break;
			case 'r':
				qstr++;
				if (qstr[0] == 'n')
					pkg_sbuf_printf(dest, "%rn", data);
				else if (qstr[0] == 'o')
					pkg_sbuf_printf(dest, "%ro", data);
				else if (qstr[0] == 'v')
					pkg_sbuf_printf(dest, "%rv", data);
				break;
			case 'C':
				pkg_sbuf_printf(dest, "%Cn", data);
				break;
			case 'F':
				qstr++;
				if (qstr[0] == 'p')
					pkg_sbuf_printf(dest, "%Fn", data);
				else if (qstr[0] == 's')
					pkg_sbuf_printf(dest, "%Fs", data);
				break;
			case 'O':
				qstr++;
				if (qstr[0] == 'k')
					pkg_sbuf_printf(dest, "%On", data);
				else if (qstr[0] == 'v')
					pkg_sbuf_printf(dest, "%Ov", data);
				else if (qstr[0] == 'd') /* default value */
					pkg_sbuf_printf(dest, "%Od", data);
				else if (qstr[0] == 'D') /* description */
					pkg_sbuf_printf(dest, "%OD", data);
				break;
			case 'D':
				pkg_sbuf_printf(dest, "%Dn", data);
				break;
			case 'L':
				pkg_sbuf_printf(dest, "%Ln", data);
				break;
			case 'U':
				pkg_sbuf_printf(dest, "%Un", data);
				break;
			case 'G':
				pkg_sbuf_printf(dest, "%Gn", data);
				break;
			case 'B':
				pkg_sbuf_printf(dest, "%Bn", data);
				break;
			case 'b':
				pkg_sbuf_printf(dest, "%bn", data);
				break;
			case 'A':
				qstr++;
				if (qstr[0] == 't')
					pkg_sbuf_printf(dest, "%An", data);
				else if (qstr[0] == 'v')
					pkg_sbuf_printf(dest, "%Av", data);
				break;
			case 'M':
				if (pkg_has_message(pkg))
					pkg_sbuf_printf(dest, "%M", pkg);
				break;
			case '%':
				sbuf_putc(dest, '%');
				break;
			}
		} else  if (qstr[0] == '\\') {
			qstr++;
			switch (qstr[0]) {
			case 'n':
				sbuf_putc(dest, '\n');
				break;
			case 'a':
				sbuf_putc(dest, '\a');
				break;
			case 'b':
				sbuf_putc(dest, '\b');
				break;
			case 'f':
				sbuf_putc(dest, '\f');
				break;
			case 'r':
				sbuf_putc(dest, '\r');
				break;
			case '\\':
				sbuf_putc(dest, '\\');
				break;
			case 't':
				sbuf_putc(dest, '\t');
				break;
			}
		} else {
			sbuf_putc(dest, qstr[0]);
		}
		qstr++;
	}
	sbuf_finish(dest);
}

void
print_query(struct pkg *pkg, char *qstr, char multiline)
{
	struct sbuf		*output = sbuf_new_auto();
	struct pkg_dep		*dep    = NULL;
	struct pkg_option	*option = NULL;
	struct pkg_file		*file   = NULL;
	struct pkg_kv		*kv;

	switch (multiline) {
	case 'd':
		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			format_str(pkg, output, qstr, dep);
			printf("%s\n", sbuf_data(output));
		}
		break;
	case 'r':
		while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
			format_str(pkg, output, qstr, dep);
			printf("%s\n", sbuf_data(output));
		}
		break;
	case 'C':
		pkg_printf("%C%{%Cn\n%|%}", pkg);
		break;
	case 'O':
		while (pkg_options(pkg, &option) == EPKG_OK) {
			format_str(pkg, output, qstr, option);
			printf("%s\n", sbuf_data(output));
		}
		break;
	case 'F':
		while (pkg_files(pkg, &file) == EPKG_OK) {
			format_str(pkg, output, qstr, file);
			printf("%s\n", sbuf_data(output));
		}
		break;
	case 'D':
		pkg_printf("%D", pkg);
		break;
	case 'L':
		pkg_printf("%L%{%Ln\n%|%}", pkg);
		break;
	case 'U':
		pkg_printf("%U", pkg);
		break;
	case 'G':
		pkg_printf("%G", pkg);
		break;
	case 'B':
		pkg_printf("%B", pkg);
		break;
	case 'b':
		pkg_printf("%b", pkg);
		break;
	case 'A':
		pkg_get(pkg, PKG_ANNOTATIONS, &kv);
		while (kv != NULL) {
			format_str(pkg, output, qstr, kv);
			printf("%s\n", sbuf_data(output));
			kv = kv->next;
		}
		break;
	default:
		format_str(pkg, output, qstr, dep);
		printf("%s\n", sbuf_data(output));
		break;
	}
	sbuf_delete(output);
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
format_sql_condition(const char *str, struct sbuf *sqlcond, bool for_remote)
{
	state_t state = NONE;
	unsigned int bracket_level = 0;

	sbuf_cat(sqlcond, " WHERE ");
	while (str[0] != '\0') {
		if (state == NONE) {
			if (str[0] == '%') {
				str++;
				switch (str[0]) {
				case 'n':
					sbuf_cat(sqlcond, "name");
					state = OPERATOR_STRING;
					break;
				case 'o':
					sbuf_cat(sqlcond, "origin");
					state = OPERATOR_STRING;
					break;
				case 'p':
					sbuf_cat(sqlcond, "prefix");
					state = OPERATOR_STRING;
					break;
				case 'm':
					sbuf_cat(sqlcond, "maintainer");
					state = OPERATOR_STRING;
					break;
				case 'c':
					sbuf_cat(sqlcond, "comment");
					state = OPERATOR_STRING;
					break;
				case 'w':
					sbuf_cat(sqlcond, "www");
					state = OPERATOR_STRING;
					break;
				case 's':
					sbuf_cat(sqlcond, "flatsize");
					state = OPERATOR_INT;
					break;
				case 'a':
					if (for_remote)
						goto bad_option;
					sbuf_cat(sqlcond, "automatic");
					state = OPERATOR_INT;
					break;
				case 'q':
					sbuf_cat(sqlcond, "arch");
					state = OPERATOR_STRING;
					break;
				case 'k':
					if (for_remote)
						goto bad_option;
					sbuf_cat(sqlcond, "locked");
					state = OPERATOR_INT;
					break;
				case 'M':
					if (for_remote)
						goto bad_option;
					sbuf_cat(sqlcond, "message");
					state = OPERATOR_STRING;
					break;
				case 't':
					if (for_remote)
						goto bad_option;
					sbuf_cat(sqlcond, "time");
					state = OPERATOR_INT;
					break;
				case 'e':
					sbuf_cat(sqlcond, "desc");
					state = OPERATOR_STRING;
					break;
				case '#': /* FALLTHROUGH */
				case '?':
					str++;
					const char *sqlop = (str[0] == '#' ? "COUNT(*)" : "COUNT(*) > 0");
					switch (str[0]) {
						case 'd':
							sbuf_printf(sqlcond, "(SELECT %s FROM deps AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'r':
							sbuf_printf(sqlcond, "(SELECT %s FROM deps AS d WHERE d.origin=p.origin)", sqlop);
							break;
						case 'C':
							sbuf_printf(sqlcond, "(SELECT %s FROM pkg_categories AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'F':
							if (for_remote)
								goto bad_option;
							sbuf_printf(sqlcond, "(SELECT %s FROM files AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'O':
							sbuf_printf(sqlcond, "(SELECT %s FROM option JOIN pkg_option USING(option_id) AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'D':
							if (for_remote)
								goto bad_option;
							sbuf_printf(sqlcond, "(SELECT %s FROM pkg_directories AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'L':
							sbuf_printf(sqlcond, "(SELECT %s FROM pkg_licenses AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'U':
							if (for_remote)
								goto bad_option;
							sbuf_printf(sqlcond, "(SELECT %s FROM pkg_users AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'G':
							if (for_remote)
								goto bad_option;
							sbuf_printf(sqlcond, "(SELECT %s FROM pkg_groups AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'B':
							sbuf_printf(sqlcond, "(SELECT %s FROM pkg_shlibs_required AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'b':
							sbuf_printf(sqlcond, "(SELECT %s FROM pkg_shlibs_provided AS d WHERE d.package_id=p.id)", sqlop);
							break;
						case 'A':
							sbuf_printf(sqlcond, "(SELECT %s FROM pkg_annotation AS d WHERE d.package_id=p.id)", sqlop);
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
					sbuf_putc(sqlcond, str[0]);
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
				sbuf_putc(sqlcond, str[0]);
				break;
			case ' ':
			case '\t':
				break;
			case '|':
				if (str[1] == '|') {
					str++;
					state = NONE;
					sbuf_cat(sqlcond, " OR ");
					break;
				} else {
					fprintf(stderr, "unexpected character %c\n", str[1]);
					return (EPKG_FATAL);
				}
			case '&':
				if (str[1] == '&') {
					str++;
					state = NONE;
					sbuf_cat(sqlcond, " AND ");
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
				sbuf_cat(sqlcond, " GLOB ");
			} else if (str[0] == '>' || str[0] == '<') {
				if (state != OPERATOR_INT) {
					fprintf(stderr, "> expected only for integers\n");
					return (EPKG_FATAL);
				}
				state = NEXT_IS_INT;
				sbuf_putc(sqlcond, str[0]);
				if (str[1] == '=') {
					str++;
					sbuf_putc(sqlcond, str[0]);
				}
			} else if (str[0] == '=') {
				if (state == OPERATOR_STRING) {
					state = NEXT_IS_STRING;
				} else {
					state = NEXT_IS_INT;
				}
				sbuf_putc(sqlcond, str[0]);
				if (str[1] == '=') {
					str++;
					sbuf_putc(sqlcond, str[0]);
				}
			} else if (str[0] == '!') {
				if (str[1] != '=') {
					fprintf(stderr, "expecting = after !\n");
					return (EPKG_FATAL);
				}
				if (state == OPERATOR_STRING) {
					state = NEXT_IS_STRING;
				} else {
					state = NEXT_IS_INT;
				}
				sbuf_putc(sqlcond, str[0]);
				str++;
				sbuf_putc(sqlcond, str[0]);
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
					sbuf_putc(sqlcond, '\'');
				} else {
					if (!isdigit(str[0])) {
						fprintf(stderr, "a number is expected, got: %c\n", str[0]);
						return (EPKG_FATAL);
					}
					state = INT;
					sbuf_putc(sqlcond, str[0]);
				}
			}
		} else if (state == INT) {
			if (!isdigit(str[0])) {
				state = POST_EXPR;
				str--;
			} else {
				sbuf_putc(sqlcond, str[0]);
			}
		} else if (state == STRING || state == QUOTEDSTRING || state == SQUOTEDSTRING) {
			if ((state == STRING && isspace(str[0])) ||
			    (state == QUOTEDSTRING && str[0] == '"') ||
			    (state == SQUOTEDSTRING && str[0] == '\'')) {
				sbuf_putc(sqlcond, '\'');
				state = POST_EXPR;
			} else {
				sbuf_putc(sqlcond, str[0]);
				if (str[0] == '\'')
					sbuf_putc(sqlcond, str[0]);
				else if (str[0] == '%' && for_remote)
					sbuf_putc(sqlcond, str[0]);
			}
		}
		str++;
	}
	if (state == STRING) {
		sbuf_putc(sqlcond, '\'');
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
	int			 retcode = EX_OK;
	int			 i;
	char			 multiline = 0;
	char			*condition = NULL;
	struct sbuf		*sqlcond = NULL;
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
			match = MATCH_CONDITION;
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
			return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		usage_query();
		return (EX_USAGE);
	}

	/* Default to all packages if no pkg provided */
	if (argc == 1 && pkgname == NULL && condition == NULL && match == MATCH_EXACT) {
		match = MATCH_ALL;
	} else if (((argc == 1) ^ (match == MATCH_ALL)) && pkgname == NULL
			&& condition == NULL) {
		usage_query();
		return (EX_USAGE);
	}

	if (analyse_query_string(argv[0], accepted_query_flags, q_flags_len,
			&query_flags, &multiline) != EPKG_OK)
		return (EX_USAGE);

	if (pkgname != NULL) {
		pkg_manifest_keys_new(&keys);
		if (pkg_open(&pkg, pkgname, keys, 0) != EPKG_OK) {
			return (EX_IOERR);
		}

		pkg_manifest_keys_free(keys);
		print_query(pkg, argv[0], multiline);
		pkg_free(pkg);
		return (EX_OK);
	}

	if (condition != NULL) {
		sqlcond = sbuf_new_auto();
		if (format_sql_condition(condition, sqlcond, false) != EPKG_OK) {
			sbuf_delete(sqlcond);
			return (EX_USAGE);
		}
		sbuf_finish(sqlcond);
	}

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to query the package database");
		return (EX_NOPERM);
	} else if (ret == EPKG_ENODB) {
		if (!quiet)
			warnx("No packages installed");
		return (EX_OK);
	} else if (ret != EPKG_OK)
		return (EX_IOERR);

	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if (match == MATCH_ALL || match == MATCH_CONDITION) {
		const char *condition_sql = NULL;
		if (match == MATCH_CONDITION && sqlcond)
			condition_sql = sbuf_data(sqlcond);
		if ((it = pkgdb_query(db, condition_sql, match)) == NULL)
			return (EX_IOERR);

		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK)
			print_query(pkg, argv[0],  multiline);

		if (ret != EPKG_END)
			retcode = EX_SOFTWARE;

		pkgdb_it_free(it);
	} else {
		int nprinted = 0;
		for (i = 1; i < argc; i++) {
			pkgname = argv[i];

			if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
				retcode = EX_IOERR;
				goto cleanup;
			}

			while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
				nprinted++;
				print_query(pkg, argv[0], multiline);
			}

			if (ret != EPKG_END) {
				retcode = EX_SOFTWARE;
				break;
			}

			pkgdb_it_free(it);
		}
		if (nprinted == 0 && retcode == EX_OK) {
			/* ensure to return a non-zero status when no package
			 were found. */
			retcode = EX_UNAVAILABLE;
		}
	}

cleanup:
	pkg_free(pkg);

	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (retcode);
}
