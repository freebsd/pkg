/*
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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
#include <inttypes.h>
#define _WITH_DPRINTF
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pkg.h"

#define PP_ALTERNATE_FORM1	(1U << 0) /* ? */
#define PP_ALTERNATE_FORM2	(1U << 1) /* # */
#define PP_LEFT_ALIGN		(1U << 2) /* - */
#define PP_EXPLICIT_PLUS	(1U << 3) /* + */
#define PP_SPACE_FOR_PLUS	(1U << 4) /* SPACE */
#define PP_ZERO_PAD		(1U << 5) /* 0 */
#define PP_THOUSANDS_SEP	(1U << 6) /* ' */

#define PP_LIC_SINGLE	0
#define PP_LIC_OR	1
#define PP_LIC_AND	2

static const char	*liclog_str[3][3] = {
	[PP_LIC_SINGLE] = { "single", "",  "==" },
	[PP_LIC_OR]     = { "or",     "|", "||" },
	[PP_LIC_AND]    = { "and",    "&", "&&" },
};

static const char	*boolean_str[2][3] = {
	[false]	= { "0", "no", "false" },
	[true]  = { "1", "yes", "true" },
};

struct percent_esc {
	unsigned	 flags;
	int		 width;
	char		*list_item_fmt;
	char		*list_sep_fmt;
	char		 fmt_code;
};

static void
free_percent_esc(struct percent_esc *p)
{
	if (p->list_item_fmt != NULL)
		free(p->list_item_fmt);
	if (p->list_sep_fmt != NULL)
		free(p->list_sep_fmt);
	free(p);
	return;
}

static struct percent_esc *
new_percent_esc(struct percent_esc *p)
{
	if (p == NULL)
		p = calloc(1, sizeof(struct percent_esc));
	else {
		p->flags = 0;
		p->width = 0;
		if (p->list_item_fmt)
			free(p->list_item_fmt);
		p->list_item_fmt = NULL;
		if (p->list_sep_fmt)
			free(p->list_sep_fmt);
		p->list_sep_fmt = NULL;
		p->fmt_code = '\0';
	}
	return (p);
}

static const char*
maybe_read_hex_byte(struct sbuf *sbuf, const char *f)
{
	int	val;

	/* Hex escapes are of the form \xNN -- always two hex digits */

	if (isxdigit(f[0]) && isxdigit(f[1])) {
		switch(*f) {
		case '0':
			val = 0x0;
			break;
		case '1':
			val = 0x10;
			break;
		case '2':
			val = 0x20;
			break;
		case '3':
			val = 0x30;
			break;
		case '4':
			val = 0x40;
			break;
		case '5':
			val = 0x50;
			break;
		case '6':
			val = 0x60;
			break;
		case '7':
			val = 0x70;
			break;
		case '8':
			val = 0x80;
			break;
		case '9':
			val = 0x90;
			break;
		case 'a':
		case 'A':
			val = 0xa0;
			break;
		case 'b':
		case 'B':
			val = 0xb0;
			break;
		case 'c':
		case 'C':
			val = 0xc0;
			break;
		case 'd':
		case 'D':
			val = 0xd0;
			break;
		case 'e':
		case 'E':
			val = 0xe0;
			break;
		case 'f':
		case 'F':
			val = 0xf0;
			break;
		}

		f++;

		switch(*f) {
		case '0':
			val += 0x0;
			break;
		case '1':
			val += 0x1;
			break;
		case '2':
			val += 0x2;
			break;
		case '3':
			val += 0x3;
			break;
		case '4':
			val += 0x4;
			break;
		case '5':
			val += 0x5;
			break;
		case '6':
			val += 0x6;
			break;
		case '7':
			val += 0x7;
			break;
		case '8':
			val += 0x8;
			break;
		case '9':
			val += 0x9;
			break;
		case 'a':
		case 'A':
			val += 0xa;
			break;
		case 'b':
		case 'B':
			val += 0xb;
			break;
		case 'c':
		case 'C':
			val += 0xc;
			break;
		case 'd':
		case 'D':
			val += 0xd;
			break;
		case 'e':
		case 'E':
			val += 0xe;
			break;
		case 'f':
		case 'F':
			val += 0xf;
			break;
		}

		sbuf_putc(sbuf, val);
	} else {
		/* Pass through unchanged if it's not a recognizable
		   hex byte. */
		sbuf_putc(sbuf, '\\');
		sbuf_putc(sbuf, 'x');
	}
	return (f);
}

static const char*
read_oct_byte(struct sbuf *sbuf, const char *f)
{
	int	val = 0;

	/* Octal escapes are upto three octal digits: \N, \NN or \NNN
	   up to a max of \377.  Note: this treats \400 as \40
	   followed by digit 0 passed through unchanged. */

	while (val < 32) {
		switch (*f) {
		case '0':
			val = val * 8 + 0;
			break;
		case '1':
			val = val * 8 + 1;
			break;
		case '2':
			val = val * 8 + 2;
			break;
		case '3':
			val = val * 8 + 3;
			break;
		case '4':
			val = val * 8 + 4;
			break;
		case '5':
			val = val * 8 + 5;
			break;
		case '6':
			val = val * 8 + 6;
			break;
		case '7':
			val = val * 8 + 7;
			break;
		default:	/* Non-octal digit */
			goto done;
		}

		f++;
	} 
done:
	f--;	/* point at the last octal digit */
	sbuf_putc(sbuf, val);

	return (f);
}

static const char *
process_escape(struct sbuf *sbuf, const char *f)
{
	f++;			/* Eat the \ */

	switch (*f) {
	case 'a':
		sbuf_putc(sbuf, '\a');
		break;
	case 'b':
		sbuf_putc(sbuf, '\b');
		break;
	case 'f':
		sbuf_putc(sbuf, '\f');
		break;
	case 'n':
		sbuf_putc(sbuf, '\n');
		break;
	case 't':
		sbuf_putc(sbuf, '\t');
		break;
	case 'v':
		sbuf_putc(sbuf, '\v');
		break;
	case '\'':
		sbuf_putc(sbuf, '\'');
		break;
	case '"':
		sbuf_putc(sbuf, '"');
		break;
	case '\\':
		sbuf_putc(sbuf, '\\');
		break;
	case 'x':		/* Hex escape: \xNN */
		f++;
		f = maybe_read_hex_byte(sbuf, f);
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':		/* all fall through */
		f = read_oct_byte(sbuf, f);
		break;
	default:		/* If it's not a recognised escape,
				   pass it through unchanged */
		sbuf_putc(sbuf, '\\');
		sbuf_putc(sbuf, *f);
		break;
	}

	return (f);
}

static char *
gen_format(char *buf, size_t buflen, unsigned flags, const char *tail)
{
	int	bp = 0;
	size_t	tlen;

	/* We need at least 3 characters '%' '*' '\0' and maybe 7 '%'
	   '#' '-' '+' '\'' '*' '\0' plus the length of tail */

	tlen = strlen(tail);

	if (buflen - bp < tlen + 3)
		return (NULL);

	buf[bp++] = '%';

	/* PP_ALTERNATE_FORM1 is not used by regular printf(3) */

	if (flags & PP_ALTERNATE_FORM2)
		buf[bp++] = '#';

	if (flags & PP_LEFT_ALIGN)
		buf[bp++] = '-';
	else if (flags & PP_ZERO_PAD)
		buf[bp++] = '0';

	if (buflen - bp < tlen + 2)
		return (NULL);
	
	if (flags & PP_EXPLICIT_PLUS)
		buf[bp++] = '+';
	else if (flags & PP_SPACE_FOR_PLUS)
		buf[bp++] = ' ';

	if (flags & PP_THOUSANDS_SEP)
		buf[bp++] = '\'';

	if (buflen - bp < tlen + 2)
		return (NULL);

	/* The effect of 0 meaning 'zero fill' is indisinguishable
	   from 0 meaning 'a field width of zero' */

	buf[bp++] = '*';
	buf[bp] = '\0';

	strlcat(buf, tail, sizeof(buf));

	return (buf);
}


static struct sbuf *
human_number(struct sbuf *sbuf, int64_t number, struct percent_esc *p)
{
	double		 num;
	int		 divisor;
	int		 scale;
	bool		 bin_scale;

#define MAXSCALE	7

	const char	 bin_pfx[MAXSCALE][3] =
		{ "", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei" }; 
	const char	 si_pfx[MAXSCALE][2] =
		{ "", "k", "M", "G", "T", "P", "E" };
	char		 fmt[16];

	bin_scale = ((p->flags & PP_ALTERNATE_FORM2) != 0);

	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	num = number;
	divisor = bin_scale ? 1024 : 1000;

	for (scale = 0; scale < MAXSCALE; scale++) {
		if (num <= divisor)
			break;
		num /= divisor;
	}

	if (gen_format(fmt, sizeof(fmt), p->flags, ".3f %s") == NULL)
		return (NULL);

	sbuf_printf(sbuf, fmt, p->width, num,
		    bin_scale ? bin_pfx[scale] : si_pfx[scale]);

	return (sbuf);
}

static struct sbuf *
string_val(struct sbuf *sbuf, const char *str, struct percent_esc *p)
{
	char	fmt[16];

	/* The '#' '?' '+' ' ' and '\'' modifiers have no meaning for
	   strings */

	p->flags &= ~(PP_ALTERNATE_FORM1 |
		      PP_ALTERNATE_FORM2 |
		      PP_EXPLICIT_PLUS   |
		      PP_SPACE_FOR_PLUS  |
		      PP_THOUSANDS_SEP);

	if (gen_format(fmt, sizeof(fmt), p->flags, "s") == NULL)
		return (NULL);

	sbuf_printf(sbuf, fmt, p->width, str);
	return (sbuf);
}

static struct sbuf *
int_val(struct sbuf *sbuf, int64_t value, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (human_number(sbuf, value, p));
	else {
		char	 fmt[16]; /* More than enough */

		if (gen_format(fmt, sizeof(fmt), p->flags, PRId64) == NULL)
			return (NULL);

		sbuf_printf(sbuf, fmt, p->width, value);
	}
	return (sbuf);
}

static struct sbuf *
list_count(struct sbuf *sbuf, int64_t count, struct percent_esc *p)
{
	/* Convert to 0 or 1 for %?X */
	if (p->flags & PP_ALTERNATE_FORM1)
		count = (count > 0);

	/* Turn off %#X and %?X flags, then print as a normal integer */
	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (int_val(sbuf, count, p));
}

/*
 * Note: List values -- special behaviour with ? and # modifiers.
 * Affects %B %C %D %F %G %L %O %U
 *
 * With ? -- Flag values.  Boolean.  %?X returns 0 if the %X list is
 * empty, 1 otherwise.
 *
 * With # -- Count values.  Integer.  %#X returns the number of items in
 * the %X list.
 */

/*
 * %B -- Shared Libraries.  List of shlibs required by binaries in the
 * pkg.  Optionall accepts per-field format in %{ %| %}, where %b is
 * replaced by the shlib name.  Default %{%b\n%|%}
 */
static struct sbuf *
format_shlibs(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_SHLIBS), p));
	else {
		/* @@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %C -- Categories.  List of Category names (strings). 1ary category is first.
 * Optionally accepts per-field format in %{ %| %}, where %c is replaced by the
 * category name.  Default %{%c%|, %}
 */
static struct sbuf *
format_categories(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_CATEGORIES),
				   p));
	else {
		/* @@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %D -- Directories.  List of directory names (strings) possibly with
 * other meta-data.  Optionally accepts following per-field format in
 * %{ %| %}, where %d is replaced by the directory name.  Default
 * %{%d\n%|%}
 */
static struct sbuf *
format_directories(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_DIRS), p));
	else {
		/* @@@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %F -- Files.  List of filenames (strings) possibly with other meta-data.
 * Optionally accepts following per-field format in %{ %| %}, where
 * %f is replaced by the filename, %s by the checksum.  Default %{%f\n%|%}
 */
static struct sbuf *
format_files(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_FILES), p));
	else {
		/* @@@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %G -- Groups. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %g will be replaced by each
 * groupname or %#g by the gid. Default %{%g\n%|%}
 */
static struct sbuf *
format_groups(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_GROUPS), p));
	else {
		/* @@@@@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %L -- Licences. List of string values.  Optionally accepts
 * following per-field format in %{ %| %} where %L is replaced by the
 * license name and %l by the license logic.  Default %{%L%| %l %}
 */
static struct sbuf *
format_licenses(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_LICENSES),
				   p));
	else {
		/* @@@@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %M -- Pkg message. string.  Accepts field-width, left-align
 */
static struct sbuf *
format_message(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*message;

	pkg_get(pkg, PKG_MESSAGE, &message);
	return (string_val(sbuf, message, p));
}

/*
 * %O -- Options. list of {option,value} tuples. Optionally accepts
 * following per-field format in %{ %| %}, where %k is replaced by the
 * option name and %v by the value.  Default %{%k %v\n%|%}
 */ 
static struct sbuf *
format_options(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_OPTIONS), p));
	else {
		/* @@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %U -- Users. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %u will be replaced by each
 * username or %#g by the uid. Default %{%u\n%|%}
 */
static struct sbuf *
format_users(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_USERS), p));
	else {
		/* @@@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %a -- Autoremove flag. boolean.  Accepts field-width, left-align.
 * Standard form: 0, 1.  Alternate form1: no, yes.  Alternate form2:
 * false, true
 */
static struct sbuf *
format_autoremove(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	bool	automatic;
	int	alternate;

	pkg_get(pkg, PKG_AUTOMATIC, &automatic);

	if (p->flags & PP_ALTERNATE_FORM2)
		alternate = 2;
	else if (p->flags & PP_ALTERNATE_FORM1)
		alternate = 1;
	else
		alternate = 0;

	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (string_val(sbuf, boolean_str[automatic][alternate], p));
}

/*
 * %c -- Comment. string.  Accepts field-width, left-align
 */
static struct sbuf *
format_comment(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*comment;

	pkg_get(pkg, PKG_COMMENT, &comment);
	return (string_val(sbuf, comment, p));
}

/*
 * %d -- Dependencies. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%n-%v\n" for each dependency.
 */
static struct sbuf *
format_dependencies(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_DEPS), p));
	else {
		/* @@@@@@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %i -- Additional info. string. Accepts field-width, left-align
 */
static struct sbuf *
format_add_info(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*info;

	pkg_get(pkg, PKG_INFOS, &info);
	return (string_val(sbuf, info, p));
}

/*
 * %l -- Licence logic. string.  Accepts field-width, left-align.
 * Standard form: and, or, single. Alternate form 1: &, |, ''.
 * Alternate form 2: &&, ||, ==
 */
static struct sbuf *
format_license_logic(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	lic_t	licenselogic;
	int	alternate;
	int	llogic;

	pkg_get(pkg, PKG_LICENSE_LOGIC, &licenselogic);

	switch (licenselogic) {
	case LICENSE_SINGLE:
		llogic = PP_LIC_SINGLE;
		break;
	case LICENSE_OR:
		llogic = PP_LIC_OR;
		break;
	case LICENSE_AND:
		llogic = PP_LIC_AND;
		break;
	}

	if (p->flags & PP_ALTERNATE_FORM2)
		alternate = 2;
	else if (p->flags & PP_ALTERNATE_FORM1)
		alternate = 1;
	else
		alternate = 0;

	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (string_val(sbuf, liclog_str[llogic][alternate], p));
}

/*
 * %m -- Maintainer e-mail address. string.  Accepts field-width, left-align
 */
static struct sbuf *
format_maintainer(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*maintainer;

	pkg_get(pkg, PKG_MAINTAINER, &maintainer);
	return (string_val(sbuf, maintainer, p));
}

/*
 * %n -- Package name. string.  Accepts field-width, left-align
 */
static struct sbuf *
format_name(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*name;

	pkg_get(pkg, PKG_NAME, &name);
	return (string_val(sbuf, name, p));
}

/*
 * %o -- Package origin. string.  Accepts field-width, left-align
 */
static struct sbuf *
format_origin(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*origin;

	pkg_get(pkg, PKG_ORIGIN, &origin);
	return (string_val(sbuf, origin, p));
}

/*
 * %p -- Installation prefix. string. Accepts field-width, left-align
 */
static struct sbuf *
format_prefix(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*prefix;

	pkg_get(pkg, PKG_PREFIX, &prefix);
	return (string_val(sbuf, prefix, p));
}

/*
 * %r -- Requirements. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%{%n-%v\n%|%}" for each dependency.
 */
static struct sbuf *
format_requirements(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return(list_count(sbuf, pkg_list_count(pkg, PKG_RDEPS), p));
	else {
		/* @@@@@@@@@@@@@@@@@@@@@@@ */
	}
	return (sbuf);
}

/*
 * %s -- Size of installed package. integer.  Accepts field-width,
 * left-align, zero-fill, space-for-plus, explicit-plus and
 * alternate-form.  Alternate form is a humanized number using decimal
 * exponents (k, M, G).  Alternate form 2, ditto, but using binary
 * scale prefixes (ki, Mi, Gi etc.)
 */
static struct sbuf *
format_flatsize(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	int64_t	flatsize;

	pkg_get(pkg, PKG_FLATSIZE, &flatsize);
	return (int_val(sbuf, flatsize, p));
}

/*
 * %t -- Installation timestamp (Unix time). integer.  Accepts
 * field-width, left-align.  Can be followed by optional strftime
 * format string in %{ %}.  Default is to print seconds-since-epoch as
 * an integer applying our format modifiers.
 */
static struct sbuf *
format_install_tstamp(struct sbuf *sbuf, struct pkg *pkg,
		      struct percent_esc *p)
{
	int64_t	 timestamp;

	pkg_get(pkg, PKG_TIME, &timestamp);

	if (p->list_item_fmt == NULL)
		return (int_val(sbuf, timestamp, p));
	else {
		char	 buf[1024];

		strftime(buf, sizeof(buf), p->list_item_fmt,
			 localtime(&timestamp));
		sbuf_cat(sbuf, buf); 
	}
	return (sbuf);
}

/*
 * %v -- Package version. string. Accepts field width, left align
 */
static struct sbuf *
format_version(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*version;

	pkg_get(pkg, PKG_VERSION, &version);
	return (string_val(sbuf, version, p));
}

/*
 * %w -- Home page URL.  string.  Accepts field width, left align
 */
static struct sbuf *
format_home_url(struct sbuf *sbuf, struct pkg *pkg,
		struct percent_esc *p)
{
	char	*url;

	pkg_get(pkg, PKG_WWW, &url);
	return (string_val(sbuf, url, p));
}

static const char *
parse_escape(const char *f, struct percent_esc **p)
{
	const char	*fstart;
	bool		 done = false;

	fstart = f;

	f++;			/* Eat the % */

	*p = new_percent_esc(*p);

	/* Field modifiers, if any:
	 * '#' alternate form
	 * '-' left align
	 * '+' explicit plus sign (numerics only)
	 * ' ' space instead of plus sign (numerics only)
	 * '0' pad with zeroes (numerics only)
	 * Note '*' (dynamic field width) is not supported
	 */

	while (!done) {
		switch (*f) {
		case '#':
			(*p)->flags |= PP_ALTERNATE_FORM1;
			break;
		case '?':
			(*p)->flags |= PP_ALTERNATE_FORM2;
			break;
		case '-':
			(*p)->flags |= PP_LEFT_ALIGN;
			break;
		case '+':
			(*p)->flags |= PP_EXPLICIT_PLUS;
			break;
		case ' ':
			(*p)->flags |= PP_SPACE_FOR_PLUS;
			break;
		case '0':
			(*p)->flags |= PP_ZERO_PAD;
			break;
		case '\'':
			(*p)->flags |= PP_THOUSANDS_SEP;
			break;
		default:
			done = true;
			break;
		}
		if (!done)
			f++;
	}

	/* Field width, if any -- some number of decimal digits.
	   Note: field width set to zero could be interpreted as using
	   0 to request zero padding: it doesn't matter which -- the
	   result on the output is exactly the same. */

	done = false;
	while (!done) {
		switch(*f) {
		case '0':
			(*p)->width = (*p)->width * 10 + 0;
			break;
		case '1':
			(*p)->width = (*p)->width * 10 + 1;
			break;
		case '2':
			(*p)->width = (*p)->width * 10 + 2;
			break;
		case '3':
			(*p)->width = (*p)->width * 10 + 3;
			break;
		case '4':
			(*p)->width = (*p)->width * 10 + 4;
			break;
		case '5':
			(*p)->width = (*p)->width * 10 + 5;
			break;
		case '6':
			(*p)->width = (*p)->width * 10 + 6;
			break;
		case '7':
			(*p)->width = (*p)->width * 10 + 7;
			break;
		case '8':
			(*p)->width = (*p)->width * 10 + 8;
			break;
		case '9':
			(*p)->width = (*p)->width * 10 + 9;
			break;
		default:
			done = true;
			break;
		}
		if (!done)
			f++;
	}

	(*p)->fmt_code = *f++;


	return (f);
}

static const char *
process_format(struct sbuf *sbuf, const char *f, struct pkg *pkg)
{
	const char		*fstart;
	struct sbuf		*s;
	struct percent_esc	*p = NULL;

	fstart = f;
	f = parse_escape(f, &p);

	/* Format code */
	switch (p->fmt_code) {
	case '%':		/* literal % */
		sbuf_putc(sbuf, '%');
		break;
	case 'B':		/* shared libraries */
		s = format_shlibs(sbuf, pkg, p);
		break;
	case 'C':		/* categories */
		s = format_categories(sbuf, pkg, p);
		break;
	case 'D':		/* directories */
		s = format_directories(sbuf, pkg, p);
		break;
	case 'F':		/* files */
		s = format_files(sbuf, pkg, p);
		break;
	case 'G':		/* groups */
		s = format_groups(sbuf, pkg, p);
		break;
	case 'L':		/* licenses */
		s = format_licenses(sbuf, pkg, p);
		break;
	case 'M':		/* message */
		s = format_message(sbuf, pkg, p);
		break;
	case 'O':		/* options */
		s = format_options(sbuf, pkg, p);
		break;
	case 'U':		/* users */
		s = format_users(sbuf, pkg, p);
		break;
	case 'a':		/* autoremove flag */
		s = format_autoremove(sbuf, pkg, p);
		break;
	case 'c':		/* comment */
		s = format_comment(sbuf, pkg, p);
		break;
	case 'd':		/* dependencies */
		s = format_dependencies(sbuf, pkg, p);
		break;
	case 'i':		/* additional info */
		s = format_add_info(sbuf, pkg, p);
		break;
	case 'l':		/* license logic */
		s = format_license_logic(sbuf, pkg, p);
		break;
	case 'm':		/* maintainer */
		s = format_maintainer(sbuf, pkg, p);
		break;
	case 'n':		/* name */
		s = format_name(sbuf, pkg, p);
		break;
	case 'o':		/* origin */
		s = format_origin(sbuf, pkg, p);
		break;
	case 'p':		/* prefix */
		s = format_prefix(sbuf, pkg, p);
		break;
	case 'r':		/* requirements */
		s = format_requirements(sbuf, pkg, p);
		break;
	case 's':		/* flat size */
		s = format_flatsize(sbuf, pkg, p);
		break;
	case 't':		/* installation timestamp */
		s = format_install_tstamp(sbuf, pkg, p);
		break;
	case 'v':		/* version */
		s = format_version(sbuf, pkg, p);
		break;
	case 'w':		/* pkg home page URL */
		s = format_home_url(sbuf, pkg, p);
		break;
	default:
		/* If it's not a known escape, pass through unchanged */
		sbuf_putc(sbuf, '%');
		f = fstart;
		break;
	}

	if (s == NULL)
		f = fstart;	/* Pass through unprocessed on error */

	free_percent_esc(p);

	return (f);
}

/**
 * print to stdout data from pkg as indicated by the format code fmt
 * @param pkg The struct pkg supplying the data
 * @param fmt String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_printf(const char *fmt, struct pkg *pkg)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;

	sbuf = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = printf("%s", sbuf_data(sbuf));
	}
	sbuf_delete(sbuf);
	return (count);
}

/**
 * print to named stream from pkg as indicated by the format code fmt
 * @param pkg The struct pkg supplying the data
 * @param fmt String with embedded %-escapes indicating what to output
 * @return count of the number of characters printed
 */
int
pkg_fprintf(FILE *stream, const char *fmt, struct pkg *pkg)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;

	sbuf = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = fprintf(stream, "%s", sbuf_data(sbuf));
	}
	sbuf_delete(sbuf);
	return (count);
}

/**
 * print to file descriptor d data from pkg as indicated by the format
 * code fmt
 * @param d Previously opened file descriptor to print to
 * @param pkg The struct pkg supplying the data
 * @param fmt String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_dprintf(int fd, const char *fmt, struct pkg *pkg)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;

	sbuf = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = dprintf(fd, "%s", sbuf_data(sbuf));
	}
	sbuf_delete(sbuf);
	return (count);
}

/**
 * print to buffer str of given size data from pkg as indicated by the
 * format code fmt as a NULL-terminated string
 * @param str Character array buffer to receive output
 * @param size Length of the buffer str
 * @param pkg The struct pkg supplying the data
 * @param fmt String with embedded %-escapes indicating what to output
 * @return count of the number of characters that would have been output
 * disregarding truncation to fit size
 */
int
pkg_snprintf(char *str, size_t size, const char *fmt, struct pkg *pkg)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;

	sbuf = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = snprintf(str, size, "%s", sbuf_data(sbuf));
	}
	sbuf_delete(sbuf);
	return (count);
}

/**
 * Allocate a string buffer ret sufficiently big to contain formatted
 * data data from pkg as indicated by the format code fmt
 * @param ret location of pointer to be set to point to buffer containing
 * result 
 * @param pkg The struct pkg supplying the data
 * @param fmt String with embedded %-escapes indicating what to output
 * @return count of the number of characters printed
 */
int
pkg_asprintf(char **ret, const char *fmt, struct pkg *pkg)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;

	sbuf = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = asprintf(ret, "%s", sbuf_data(sbuf));
	} else {
		count = -1;
		*ret = NULL;
	}
	sbuf_delete(sbuf);
	return (count);
}

/**
 * store data from pkg into sbuf as indicated by the format code fmt.
 * This is the core function called by all the other pkg_printf() family.
 * @param sbuf contains the result
 * @param pkg The struct pkg supplying the data
 * @param fmt String with embedded %-escapes indicating what to output
 * @return count of the number of characters in the result
 */
struct sbuf *
pkg_sbuf_printf(struct sbuf *sbuf, const char *fmt, struct pkg *pkg)
{
	const char	*f;

	for (f = fmt; *f != '\0'; f++) {
		if (*f == '%') {
			f = process_format(sbuf, f, pkg);
		} else if (*f == '\\' ) {
			f = process_escape(sbuf, f);
		} else {
			sbuf_putc(sbuf, *f);
		}
	}
	return (sbuf);
}

/*
 * That's All Folks!
 */
