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
#include <stdarg.h>
#define _WITH_DPRINTF
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pkg.h"

/*
 * Format codes
 *    Arg Type     What
 * A
 *
 * B  pkg          List of shared libraries
 * Bn pkg_shlib    Shared library name
 *
 * C  pkg          List of categories
 * Cn pkg_category Category name
 *
 * D  pkg          List of directories
 * Dg pkg_dir      Group owner of directory
 * Dk pkg_dir      Keep flag
 * Dn pkg_dir      Directory path name
 * Dp pkg_dir      Directory permissions
 * Dt pkg_dir      Try flag (@dirrmtry in plist)
 * Du pkg_dir      User owner of directory
 * 
 * E
 *
 * F  pkg          List of files
 * Fg pkg_file     Group owner of file
 * Fk pkg_file     Keep flag
 * Fn pkg_file     File path name
 * Fp pkg_file     File permissions
 * Fs pkg_file     File SHA256 checksum
 * Fu pkg_file     User owner of file 
 *
 * G  pkg          List of groups
 * Gg pkg_group    gidstr (parse this using gr_scan()?)
 * Gn pkg_group    Group name
 *
 * H
 *
 * I  int*         Row counter
 *
 * J
 * K
 *
 * L  pkg          List of licenses
 * Ln pkg_license  Licence name
 *
 * M  pkg          Message
 *
 * N
 *
 * O  pkg          List of options
 * On pkg_option   Option name (key)
 * Ov pkg_option   Option value
 *
 * P
 * Q
 * R
 * S
 * T
 *
 * U  pkg          List of users
 * Un pkg_user     User name
 * Uu pkg_user     uidstr (parse this using pw_scan()?)
 *
 * V
 * W
 * X
 * Y
 * Z
 *
 * a  pkg          autoremove flag
 *
 * b
 *
 * c  pkg          comment
 *
 * d  pkg          List of dependencies
 * dn pkg_dep      dependency name
 * do pkg_dep      dependency origin
 * dv pkg_dep      dependency version
 *
 * e
 * f
 * g
 * h
 *
 * i  pkg          additional info
 *
 * j
 * k
 *
 * l  pkg          license logic
 *
 * m  pkg          maintainer
 *
 * n  pkg          name
 *
 * o  pkg          origin
 *
 * p  pkg          prefix
 *
 * q
 *
 * r  pkg          List of requirements
 * rn pkg_dep      requirement name
 * ro pkg_dep      requirement origin
 * rv pkg_dep      requirement version
 *
 * s  pkg          flatsize
 *
 * t  pkg          install timestamp
 *
 * u
 *
 * v  pkg          version
 *
 * w  pkg          home page URL
 *
 * x
 * y
 * z
 */

/* Format code modifiers */
#define PP_ALTERNATE_FORM1	(1U << 0) /* ? */
#define PP_ALTERNATE_FORM2	(1U << 1) /* # */
#define PP_LEFT_ALIGN		(1U << 2) /* - */
#define PP_EXPLICIT_PLUS	(1U << 3) /* + */
#define PP_SPACE_FOR_PLUS	(1U << 4) /* SPACE */
#define PP_ZERO_PAD		(1U << 5) /* 0 */
#define PP_THOUSANDS_SEP	(1U << 6) /* ' */

/* Contexts for option parsing */
#define PP_PKG	(1U << 0)	/* Any pkg scalar value */
#define PP_B	(1U << 1)	/* shlib */
#define PP_C	(1U << 2)	/* category */
#define PP_D	(1U << 3)	/* directory */
#define PP_F	(1U << 4)	/* file */
#define PP_G	(1U << 5)	/* group */
#define PP_L	(1U << 6)	/* licence */
#define PP_O	(1U << 7)	/* option */
#define PP_U	(1U << 8)	/* user */
#define PP_d	(1U << 9)	/* dependency */
#define PP_r	(1U << 10)	/* requirement */

#define _PP_last	PP_r
#define PP_ALL	((_PP_last << 1) - 1) /* All contexts */

/* Licence logic types */
#define PP_LIC_SINGLE	0
#define PP_LIC_OR	1
#define PP_LIC_AND	2

/* These are in ASCII order of format code: ie alphabetical with A-Z
 * sorting before a-z */
typedef enum _fmt_code_t {
	PP_PKG_SHLIBS = 0,
	PP_PKG_SHLIB_NAME,
	PP_PKG_CATEGORIES,
	PP_PKG_CATEGORY_NAME,
	PP_PKG_DIRECTORIES,
	PP_PKG_DIRECTORY_GROUP,
	PP_PKG_DIRECTORY_KEEPFLAG,
	PP_PKG_DIRECTORY_PATH,
	PP_PKG_DIRECTORY_PERMS,
	PP_PKG_DIRECTORY_TRYFLAG,
	PP_PKG_DIRECTORY_USER,
	PP_PKG_FILES,
	PP_PKG_FILE_GROUP,
	PP_PKG_FILE_KEEPFLAG,
	PP_PKG_FILE_PATH,
	PP_PKG_FILE_PERMS,
	PP_PKG_FILE_SHA256,
	PP_PKG_FILE_USER,
	PP_PKG_GROUPS,
	PP_PKG_GROUP_GIDSTR,
	PP_PKG_GROUP_NAME,
	PP_ROW_COUNTER,
	PP_PKG_LICENSES,
	PP_PKG_LICENSE_NAME,
	PP_PKG_MESSAGE,
	PP_PKG_OPTIONS,
	PP_PKG_OPTION_NAME,
	PP_PKG_OPTION_VALUE,
	PP_PKG_USERS,
	PP_PKG_USER_NAME,
	PP_PKG_USER_UIDSTR,
	PP_PKG_AUTOREMOVE,
	PP_PKG_COMMENT,
	PP_PKG_DEPENDENCIES,
	PP_PKG_DEPENDENCY_NAME,
	PP_PKG_DEPENDENCY_ORIGIN,
	PP_PKG_DEPENDENCY_VERSION,
	PP_PKG_ADDITIONAL_INFO,
	PP_PKG_LICENSE_LOGIC,
	PP_PKG_MAINTAINER,
	PP_PKG_NAME,
	PP_PKG_ORIGIN,
	PP_PKG_PREFIX,
	PP_PKG_REQUIREMENTS,
	PP_PKG_REQUIREMENT_NAME,
	PP_PKG_REQUIREMENT_ORIGIN,
	PP_PKG_REQUIREMENT_VERSION,
	PP_PKG_FLATSIZE,
	PP_PKG_INSTALL_TIMESTAMP,
	PP_PKG_VERSION,
	PP_PKG_HOME_PAGE,
	PP_LAST_FORMAT = PP_PKG_HOME_PAGE,
	PP_LITERAL_PERCENT,
	PP_END_MARKER,
} fmt_code_t;

struct pkg_printf_fmt {
	char	         fmt_main;
	char		 fmt_sub;
	unsigned	 context;
	/*
	struct sbuf	*(*fmt_handler)(struct sbuf *, const void *,
					struct percent_esc *);
	*/
};

struct percent_esc {
	unsigned	 flags;
	int		 width;
	struct sbuf	*item_fmt;
	struct sbuf	*sep_fmt;
	fmt_code_t	 fmt_code;
};

/* Format handler function prototypes */



/* These are in ASCII order: alphabetical with A-Z sorting before a-z */
static const struct pkg_printf_fmt	fmt[] = {
	[PP_PKG_SHLIBS] =		{ 'B',  '\0', PP_PKG,      },
	[PP_PKG_SHLIB_NAME] =		{ 'B',  'n',  PP_PKG|PP_B, },
	[PP_PKG_CATEGORIES] =		{ 'C',  '\0', PP_PKG,      },
        [PP_PKG_CATEGORY_NAME] =	{ 'C',  'n',  PP_PKG|PP_C, },
	[PP_PKG_DIRECTORIES] =		{ 'D',  '\0', PP_PKG,      },
        [PP_PKG_DIRECTORY_GROUP] =	{ 'D',  'g',  PP_PKG|PP_D, },
	[PP_PKG_DIRECTORY_KEEPFLAG] =	{ 'D',  'k',  PP_PKG|PP_D, },
	[PP_PKG_DIRECTORY_PATH] =       { 'D',  'n',  PP_PKG|PP_D, },
	[PP_PKG_DIRECTORY_PERMS] =	{ 'D',  'p',  PP_PKG|PP_D, },
	[PP_PKG_DIRECTORY_TRYFLAG] =	{ 'D',  't',  PP_PKG|PP_D, },
	[PP_PKG_DIRECTORY_USER] =	{ 'D',  'u',  PP_PKG|PP_D, },
	[PP_PKG_FILES] =		{ 'F',  '\0', PP_PKG,      },
	[PP_PKG_FILE_GROUP] =		{ 'F',  'g',  PP_PKG|PP_F, },
	[PP_PKG_FILE_KEEPFLAG] =	{ 'F',  'k',  PP_PKG|PP_F, },
	[PP_PKG_FILE_PATH] =		{ 'F',  'n',  PP_PKG|PP_F, },
	[PP_PKG_FILE_PERMS] =		{ 'F',  'p',  PP_PKG|PP_F, },
	[PP_PKG_FILE_SHA256] =		{ 'F',  's',  PP_PKG|PP_F, },
	[PP_PKG_FILE_USER] =		{ 'F',  'u',  PP_PKG|PP_F, },
	[PP_PKG_GROUPS] =		{ 'G',  '\0', PP_PKG,      },
	[PP_PKG_GROUP_GIDSTR] =		{ 'G',  'g',  PP_PKG|PP_G, },
	[PP_PKG_GROUP_NAME] =		{ 'G',  'n',  PP_PKG|PP_G, },
	[PP_ROW_COUNTER] =		{ 'I',  '\0', PP_ALL,      },
	[PP_PKG_LICENSES] =		{ 'L',  '\0', PP_PKG,      },
	[PP_PKG_LICENSE_NAME] =		{ 'L',  'n',  PP_PKG|PP_L, },
	[PP_PKG_MESSAGE] =		{ 'M',  '\0', PP_ALL,      },
	[PP_PKG_OPTIONS] =		{ 'O',  '\0', PP_PKG,      },
	[PP_PKG_OPTION_NAME] =		{ 'O',  'n',  PP_PKG|PP_O, },
	[PP_PKG_OPTION_VALUE] =		{ 'O',  'v',  PP_PKG|PP_O, },
	[PP_PKG_USERS] =		{ 'U',  '\0', PP_PKG,      },
	[PP_PKG_USER_NAME] =		{ 'U',  'n',  PP_PKG|PP_U, },
	[PP_PKG_USER_UIDSTR] =		{ 'U',  'u',  PP_PKG|PP_U, },
	[PP_PKG_AUTOREMOVE] =		{ 'a',  '\0', PP_ALL,      },
	[PP_PKG_COMMENT] =		{ 'c',  '\0', PP_ALL,      },
	[PP_PKG_DEPENDENCIES] =		{ 'd',  '\0', PP_ALL,      },
	[PP_PKG_DEPENDENCY_NAME] =	{ 'd',  'n',  PP_PKG,      },
	[PP_PKG_DEPENDENCY_ORIGIN] =	{ 'd',  'o',  PP_PKG|PP_d, },
	[PP_PKG_DEPENDENCY_VERSION] =	{ 'v',  'v',  PP_PKG|PP_d, },
	[PP_PKG_ADDITIONAL_INFO] =	{ 'i',  '\0', PP_ALL,      },
	[PP_PKG_LICENSE_LOGIC] =	{ 'l',  '\0', PP_ALL,      },
	[PP_PKG_MAINTAINER] =		{ 'm',  '\0', PP_ALL,      },
	[PP_PKG_NAME] =			{ 'n',  '\0', PP_ALL,      },
	[PP_PKG_ORIGIN] =		{ 'o',  '\0', PP_ALL,      },
	[PP_PKG_PREFIX] =		{ 'p',  '\0', PP_ALL,      },
	[PP_PKG_REQUIREMENTS] =		{ 'r',  '\0', PP_PKG,      },
	[PP_PKG_REQUIREMENT_NAME] =	{ 'r',  'n',  PP_PKG|PP_r, },
	[PP_PKG_REQUIREMENT_ORIGIN] =   { 'r',  'o',  PP_PKG|PP_r, },
	[PP_PKG_REQUIREMENT_VERSION] =	{ 'r',  'v',  PP_PKG|PP_r, },
	[PP_PKG_FLATSIZE] =		{ 's',  '\0', PP_ALL,      },
	[PP_PKG_INSTALL_TIMESTAMP] =	{ 't',  '\0', PP_ALL,      },
	[PP_PKG_VERSION] =		{ 'v',  '\0', PP_ALL,      },
	[PP_PKG_HOME_PAGE] =		{ 'w',  '\0', PP_ALL,      },
	[PP_LITERAL_PERCENT] =		{ '%',  '\0', PP_ALL,      },
	[PP_END_MARKER] =		{ '\0', '\0', 0,           },

};

static const char	*liclog_str[3][3] = {
	[PP_LIC_SINGLE] = { "single", "",  "==" },
	[PP_LIC_OR]     = { "or",     "|", "||" },
	[PP_LIC_AND]    = { "and",    "&", "&&" },
};

static const char	*boolean_str[2][3] = {
	[false]	= { "0", "no", "false" },
	[true]  = { "1", "yes", "true" },
};

static void
free_percent_esc(struct percent_esc *p)
{
	if (p->item_fmt)
		sbuf_delete(p->item_fmt);
	if (p->sep_fmt)
		sbuf_delete(p->sep_fmt);
	free(p);
	return;
}

static struct percent_esc *
new_percent_esc(struct percent_esc *p)
{
	/* reset or alloc new */
	if (p == NULL) {
		p = calloc(1, sizeof(struct percent_esc));
		if (p != NULL) {
			p->item_fmt = sbuf_new_auto();
			p->sep_fmt = sbuf_new_auto();
		}
		if (p == NULL || p->item_fmt == NULL || p->sep_fmt == NULL) {
			/* out of memory */
			free_percent_esc(p);
			return NULL;
		}
	} else {
		p->flags = 0;
		p->width = 0;
		sbuf_clear(p->item_fmt);
		sbuf_clear(p->sep_fmt);
		p->fmt_code = '\0';
	}
	return (p);
}

static inline const char*
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

static inline const char*
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

static inline const char *
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

	/* We need the length of tail plus at least 3 characters '%'
	   '*' '\0' but maybe as many as 7 '%' '#' '-' '+' '\'' '*'
	   '\0' */

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
	char		 format[16];

	bin_scale = ((p->flags & PP_ALTERNATE_FORM2) != 0);

	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	num = number;
	divisor = bin_scale ? 1024 : 1000;

	for (scale = 0; scale < MAXSCALE; scale++) {
		if (num <= divisor)
			break;
		num /= divisor;
	}

	if (gen_format(format, sizeof(format), p->flags, ".3f %s") == NULL)
		return (NULL);

	sbuf_printf(sbuf, format, p->width, num,
		    bin_scale ? bin_pfx[scale] : si_pfx[scale]);

	return (sbuf);
}

static inline struct sbuf *
string_val(struct sbuf *sbuf, const char *str, struct percent_esc *p)
{
	char	format[16];

	/* The '#' '?' '+' ' ' and '\'' modifiers have no meaning for
	   strings */

	p->flags &= ~(PP_ALTERNATE_FORM1 |
		      PP_ALTERNATE_FORM2 |
		      PP_EXPLICIT_PLUS   |
		      PP_SPACE_FOR_PLUS  |
		      PP_THOUSANDS_SEP);

	if (gen_format(format, sizeof(format), p->flags, "s") == NULL)
		return (NULL);

	sbuf_printf(sbuf, format, p->width, str);
	return (sbuf);
}

static inline struct sbuf *
int_val(struct sbuf *sbuf, int64_t value, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (human_number(sbuf, value, p));
	else {
		char	 format[16];

		if (gen_format(format, sizeof(format), p->flags, PRId64)
		    == NULL)
			return (NULL);

		sbuf_printf(sbuf, format, p->width, value);
	}
	return (sbuf);
}

static inline struct sbuf *
bool_val(struct sbuf *sbuf, bool value, struct percent_esc *p)
{
	int	alternate;

	if (p->flags & PP_ALTERNATE_FORM2)
		alternate = 2;
	else if (p->flags & PP_ALTERNATE_FORM1)
		alternate = 1;
	else
		alternate = 0;

	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (string_val(sbuf, boolean_str[value][alternate], p));
}

static inline struct sbuf *
mode_val(struct sbuf *sbuf, mode_t mode, struct percent_esc *p)
{
	/* Print mode as an octal integer '%o' by default.
	 * PP_ALTERNATE_FORM2 generates '%#o' pased to regular
	 * printf(). PP_ALTERNATE_FORM1 will generate drwxr-x--- style
	 * from strmode(3).  */

	/* Does the mode include the bits that indicate the inode type? */

	if (p->flags & PP_ALTERNATE_FORM1) {
		char	modebuf[12];

		strmode(mode, modebuf);

		return (string_val(sbuf, modebuf, p));
	} else {
		char	format[16];

		p->flags &= ~(PP_ALTERNATE_FORM1);

		if (gen_format(format, sizeof(format), p->flags, PRIo16)
		    == NULL)
			return (NULL);

		sbuf_printf(sbuf, format, p->width, mode);
	}
	return (sbuf);
}

static inline struct sbuf *
list_count(struct sbuf *sbuf, int64_t count, struct percent_esc *p)
{
	/* Convert to 0 or 1 for %?X */
	if (p->flags & PP_ALTERNATE_FORM1)
		count = (count > 0);

	/* Turn off %#X and %?X flags, then print as a normal integer */
	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (int_val(sbuf, count, p));
}

static inline struct percent_esc *
set_list_defaults(struct percent_esc *p, const char *item_fmt,
		  const char *sep_fmt)
{
	if (sbuf_len(p->item_fmt) == 0) {
		sbuf_cat(p->item_fmt, item_fmt);
		sbuf_finish(p->item_fmt);
	}
	if (sbuf_len(p->sep_fmt) == 0) {
		sbuf_cat(p->sep_fmt, sep_fmt);
		sbuf_finish(p->sep_fmt);
	}
	return (p);
}

static struct sbuf *
iterate_item(struct sbuf *sbuf, struct pkg *pkg, void *data, int count,
	     const char *format, unsigned context)
{
	struct percent_esc	*p = new_percent_esc(NULL);

	/* @@@@@@@@@@@@@@@@@@@@ */

	free_percent_esc(p);

	return (sbuf);
}

/*
 * Note: List values -- special behaviour with ? and # modifiers.
 * Affects %B %C %D %F %G %L %O %U %d %r
 *
 * With ? -- Flag values.  Boolean.  %?X returns 0 if the %X list is
 * empty, 1 otherwise.
 *
 * With # -- Count values.  Integer.  %#X returns the number of items in
 * the %X list.
 */

/*
 * %B -- Shared Libraries.  List of shlibs required by binaries in the
 * pkg.  Optionall accepts per-field format in %{ %| %}, where %n is
 * replaced by the shlib name.  Default %{%Bn\n%|%}
 */
static struct sbuf *
format_shlibs(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_SHLIBS), p));
	else {
		struct pkg_shlib	*shlib;
		int			 count;

		set_list_defaults(p, "%Bn\n", "");

		count = 1;
		while (pkg_shlibs(pkg, &shlib) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, shlib, count,
					     sbuf_data(p->sep_fmt), PP_B);

			iterate_item(sbuf, pkg, shlib, count,
				     sbuf_data(p->item_fmt), PP_B);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Bn -- Shared Library name.
 */
static struct sbuf *
format_shlib_name(struct sbuf *sbuf, struct pkg_shlib *shlib,
		  struct percent_esc *p)
{
	return (string_val(sbuf, pkg_shlib_name(shlib), p));
}

/*
 * %C -- Categories.  List of Category names (strings). 1ary category
 * is not distinguished -- look at the package origin for that.
 * Optionally accepts per-field format in %{ %| %}, where %n is
 * replaced by the category name.  Default %{%Cn%|, %}
 */
static struct sbuf *
format_categories(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_CATEGORIES),
				   p));
	else {
		struct pkg_category	*cat;
		int			 count;

		set_list_defaults(p, "%Cn", ", ");

		count = 1;
		while (pkg_categories(pkg, &cat) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, cat, count,
					     sbuf_data(p->sep_fmt), PP_C);

			iterate_item(sbuf, pkg, cat, count,
				     sbuf_data(p->item_fmt), PP_C);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Cn -- Category name.
 */
static struct sbuf *
format_category_name(struct sbuf *sbuf, struct pkg_category *cat,
		     struct percent_esc *p)
{
	return (string_val(sbuf, pkg_category_name(cat), p));
}

/*
 * %D -- Directories.  List of directory names (strings) possibly with
 * other meta-data.  Optionally accepts following per-field format in
 * %{ %| %}, where %Dn is replaced by the directory name.  Default
 * %{%Dn\n%|%}
 */
static struct sbuf *
format_directories(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_DIRS), p));
	else {
		struct pkg_dir	*dir;
		int		 count;

		set_list_defaults(p, "%Dn\n", "");

		count = 1;
		while (pkg_dirs(pkg, &dir) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, dir, count,
					     sbuf_data(p->sep_fmt), PP_D);

			iterate_item(sbuf, pkg, dir, count,
				     sbuf_data(p->item_fmt), PP_D);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Dg -- Directory group. TODO: numeric gid
 */
static struct sbuf *
format_directory_group(struct sbuf *sbuf, struct pkg_dir *dir,
		       struct percent_esc *p)
{
	return (string_val(sbuf, pkg_dir_gname(dir), p));
}

/*
 * %Dk -- Directory Keep flag.
 */
static struct sbuf *
format_directory_keepflag(struct sbuf *sbuf, struct pkg_dir *dir,
		       struct percent_esc *p)
{
	return (bool_val(sbuf, pkg_dir_keep(dir), p));
}

/*
 * %Dn -- Directory path name.
 */
static struct sbuf *
format_directory_path(struct sbuf *sbuf, struct pkg_dir *dir,
		      struct percent_esc *p)
{
	return (string_val(sbuf, pkg_dir_path(dir), p));
}

/*
 * %Dp -- Directory permissions.
 */
static struct sbuf *
format_directory_perms(struct sbuf *sbuf, struct pkg_dir *dir,
		       struct percent_esc *p)
{
	return (mode_val(sbuf, pkg_dir_mode(dir), p));
}

/*
 * %Dt -- Directory Try flag.
 */
static struct sbuf *
format_directory_tryflag(struct sbuf *sbuf, struct pkg_dir *dir,
			 struct percent_esc *p)
{
	return (bool_val(sbuf, pkg_dir_try(dir), p));
}

/*
 * %Du -- Directory user. TODO: numeric UID
 */
static struct sbuf *
format_directory_user(struct sbuf *sbuf, struct pkg_dir *dir,
		      struct percent_esc *p)
{
	return (string_val(sbuf, pkg_dir_uname(dir), p));
}

/*
 * %F -- Files.  List of filenames (strings) possibly with other
 * meta-data.  Optionally accepts following per-field format in %{ %|
 * %}, where %n is replaced by the filename, %s by the checksum, etc.
 * Default %{%Fn\n%|%}
 */
static struct sbuf *
format_files(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_FILES), p));
	else {
		struct pkg_file	*file;
		int		 count;

		set_list_defaults(p, "%Fn\n", "");

		count = 1;
		while (pkg_files(pkg, &file) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, file, count,
					     sbuf_data(p->sep_fmt), PP_F);

			iterate_item(sbuf, pkg, file, count,
				     sbuf_data(p->item_fmt), PP_F);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Fg -- File group.
 */
static struct sbuf *
format_file_group(struct sbuf *sbuf, struct pkg_file *file,
		  struct percent_esc *p)
{
	return (string_val(sbuf, pkg_file_gname(file), p));
}

/*
 * %Fk -- File Keep flag.
 */
static struct sbuf *
format_file_keepflag(struct sbuf *sbuf, struct pkg_file *file,
		     struct percent_esc *p)
{
	return (bool_val(sbuf, pkg_file_keep(file), p));
}

/*
 * %Fn -- File path name.
 */
static struct sbuf *
format_file_path(struct sbuf *sbuf, struct pkg_file *file,
		  struct percent_esc *p)
{
	return (string_val(sbuf, pkg_file_path(file), p));
}

/*
 * %Fp -- File permissions.
 */
static struct sbuf *
format_file_perms(struct sbuf *sbuf, struct pkg_file *file,
		  struct percent_esc *p)
{
	return (mode_val(sbuf, pkg_file_mode(file), p));
}

/*
 * %Fs -- File SHA256 Checksum.
 */
static struct sbuf *
format_file_sha256(struct sbuf *sbuf, struct pkg_file *file,
		   struct percent_esc *p)
{
	return (string_val(sbuf, pkg_file_cksum(file), p));
}

/*
 * %Fu -- File user.
 */
static struct sbuf *
format_file_user(struct sbuf *sbuf, struct pkg_file *file,
		  struct percent_esc *p)
{
	return (string_val(sbuf, pkg_file_uname(file), p));
}

/*
 * %G -- Groups. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %Gn will be replaced by each
 * groupname or %#Gn by the gid or %Gg by the "gidstr" -- a line from
 * /etc/group. Default %{%Gn\n%|%}
 */
static struct sbuf *
format_groups(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_GROUPS), p));
	else {
		struct pkg_group	*group;
		int			 count;

		set_list_defaults(p, "%Gn\n", "");

		count = 1;
		while(pkg_groups(pkg, &group) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, group, count,
					     sbuf_data(p->sep_fmt), PP_G);

			iterate_item(sbuf, pkg, group, count,
				     sbuf_data(p->item_fmt), PP_G);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Gg -- Group 'gidstr' (one line from /etc/group).
 */
static struct sbuf *
format_group_gidstr(struct sbuf *sbuf, struct pkg_group *group,
		    struct percent_esc *p)
{
	return (string_val(sbuf, pkg_group_gidstr(group), p));
}

/*
 * %Gn -- Group name.
 */
static struct sbuf *
format_group_name(struct sbuf *sbuf, struct pkg_group *group,
		    struct percent_esc *p)
{
	return (string_val(sbuf, pkg_group_name(group), p));
}

/*
 * %I -- Row counter (integer*). Usually used only in per-field format.
 */
static struct sbuf *
format_row_counter(struct sbuf *sbuf, int *counter, struct percent_esc *p)
{
	return (int_val(sbuf, *counter, p));
}

/*
 * %L -- Licences. List of string values.  Optionally accepts
 * following per-field format in %{ %| %} where %Ln is replaced by the
 * license name and %l by the license logic.  Default %{%n%| %l %}
 */
static struct sbuf *
format_licenses(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_LICENSES),
				   p));
	else {
		struct pkg_license	*lic;
		int			 count;
		lic_t			 license_logic;


		set_list_defaults(p, "%Ln", " %l ");
		pkg_get(pkg, PKG_LICENSE_LOGIC, &license_logic);

		count = 1;
		while (pkg_licenses(pkg, &lic) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, lic, count,
					     sbuf_data(p->sep_fmt), PP_L);

			iterate_item(sbuf, pkg, lic, count,
				     sbuf_data(p->item_fmt), PP_L);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Ln -- License name.
 */
static struct sbuf *
format_license_name(struct sbuf *sbuf, struct pkg_license *license,
		    struct percent_esc *p)
{
	return (string_val(sbuf, pkg_license_name(license), p));
}

/*
 * %M -- Pkg message. string.  Accepts field-width, left-align
 */
static inline struct sbuf *
format_message(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*message;

	pkg_get(pkg, PKG_MESSAGE, &message);
	return (string_val(sbuf, message, p));
}

/*
 * %O -- Options. list of {option,value} tuples. Optionally accepts
 * following per-field format in %{ %| %}, where %On is replaced by the
 * option name and %Ov by the value.  Default %{%On %Ov\n%|%}
 */ 
static struct sbuf *
format_options(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_OPTIONS), p));
	else {
		struct pkg_option	*opt;
		int			 count;

		set_list_defaults(p, "%On %Ov\n", "");

		count = 1;
		while (pkg_options(pkg, &opt) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, opt, count,
					     sbuf_data(p->sep_fmt), PP_O);

			iterate_item(sbuf, pkg, opt, count,
				     sbuf_data(p->item_fmt), PP_O);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %On -- Option name.
 */
static struct sbuf *
format_option_name(struct sbuf *sbuf, struct pkg_option *option,
		    struct percent_esc *p)
{
	return (string_val(sbuf, pkg_option_opt(option), p));
}

/*
 * %Ov -- Option value.
 */
static struct sbuf *
format_option_value(struct sbuf *sbuf, struct pkg_option *option,
		    struct percent_esc *p)
{
	return (string_val(sbuf, pkg_option_value(option), p));
}

/*
 * %U -- Users. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %Un will be replaced by each
 * username or %#Un by the uid or %Uu by the uidstr -- a line from
 * /etc/passwd. Default %{%Un\n%|%}
 */
static struct sbuf *
format_users(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_USERS), p));
	else {
		struct pkg_user	*user;
		int		 count;

		set_list_defaults(p, "%Un\n", "");

		count = 1;
		while (pkg_users(pkg, &user) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, user, count, 
					     sbuf_data(p->sep_fmt), PP_U);

			iterate_item(sbuf, pkg, user, count,
				     sbuf_data(p->item_fmt), PP_U);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Un -- User name.
 */
static struct sbuf *
format_user_name(struct sbuf *sbuf, struct pkg_user *user,
		 struct percent_esc *p)
{
	return (string_val(sbuf, pkg_user_name(user), p));
}

/*
 * %Uu -- User uidstr (one line from /etc/passwd.
 */
static struct sbuf *
format_user_uidstr(struct sbuf *sbuf, struct pkg_user *user,
		   struct percent_esc *p)
{
	return (string_val(sbuf, pkg_user_uidstr(user), p));
}

/*
 * %a -- Autoremove flag. boolean.  Accepts field-width, left-align.
 * Standard form: 0, 1.  Alternate form1: no, yes.  Alternate form2:
 * false, true
 */
static inline struct sbuf *
format_autoremove(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	bool	automatic;

	pkg_get(pkg, PKG_AUTOMATIC, &automatic);
	return (bool_val(sbuf, automatic, p));
}

/*
 * %c -- Comment. string.  Accepts field-width, left-align
 */
static inline struct sbuf *
format_comment(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*comment;

	pkg_get(pkg, PKG_COMMENT, &comment);
	return (string_val(sbuf, comment, p));
}

/*
 * %d -- Dependencies. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%dn-%dv\n" for each dependency.
 */
static struct sbuf *
format_dependencies(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_DEPS), p));
	else {
		struct pkg_dep	*dep;
		int		 count;

		set_list_defaults(p, "%dn-%dv\n", "");

		count = 1;
		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, dep, count,
					     sbuf_data(p->sep_fmt), PP_d);

			iterate_item(sbuf, pkg, dep, count,
				     sbuf_data(p->item_fmt), PP_d);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %dn -- Dependency name or %rn -- Requirement name.
 */
static struct sbuf *
format_dependency_name(struct sbuf *sbuf, struct pkg_dep *dep,
		       struct percent_esc *p)
{
	return (string_val(sbuf, pkg_dep_name(dep), p));
}

/*
 * %do -- Dependency origin or %ro -- Requirement origin.
 */
static struct sbuf *
format_dependency_origin(struct sbuf *sbuf, struct pkg_dep *dep,
			 struct percent_esc *p)
{
	return (string_val(sbuf, pkg_dep_origin(dep), p));
}

/*
 * %dv -- Dependency version or %rv -- Requirement version.
 */
static struct sbuf *
format_dependency_version(struct sbuf *sbuf, struct pkg_dep *dep,
		       struct percent_esc *p)
{
	return (string_val(sbuf, pkg_dep_version(dep), p));
}

/*
 * %i -- Additional info. string. Accepts field-width, left-align
 */
static inline struct sbuf *
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
static inline struct sbuf *
format_maintainer(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*maintainer;

	pkg_get(pkg, PKG_MAINTAINER, &maintainer);
	return (string_val(sbuf, maintainer, p));
}

/*
 * %n -- Package name. string.  Accepts field-width, left-align
 */
static inline struct sbuf *
format_name(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*name;

	pkg_get(pkg, PKG_NAME, &name);
	return (string_val(sbuf, name, p));
}

/*
 * %o -- Package origin. string.  Accepts field-width, left-align
 */
static inline struct sbuf *
format_origin(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*origin;

	pkg_get(pkg, PKG_ORIGIN, &origin);
	return (string_val(sbuf, origin, p));
}

/*
 * %p -- Installation prefix. string. Accepts field-width, left-align
 */
static inline struct sbuf *
format_prefix(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	char	*prefix;

	pkg_get(pkg, PKG_PREFIX, &prefix);
	return (string_val(sbuf, prefix, p));
}

/*
 * %r -- Requirements. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%{%rn-%rv\n%|%}" for each dependency.
 */
static struct sbuf *
format_requirements(struct sbuf *sbuf, struct pkg *pkg, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return(list_count(sbuf, pkg_list_count(pkg, PKG_RDEPS), p));
	else {
		struct pkg_dep	*req;
		int		 count;

		set_list_defaults(p, "%rn-%rv\n", "");

		count = 1;
		while (pkg_rdeps(pkg, &req) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, req, count,
					     sbuf_data(p->sep_fmt), PP_r);

			iterate_item(sbuf, pkg, req, count,
				     sbuf_data(p->item_fmt), PP_r);
			count++;
		}
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
static inline struct sbuf *
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
 * an integer applying our integer format modifiers.
 */
static inline struct sbuf *
format_install_tstamp(struct sbuf *sbuf, struct pkg *pkg,
		      struct percent_esc *p)
{
	int64_t	 timestamp;

	pkg_get(pkg, PKG_TIME, &timestamp);

	if (sbuf_len(p->item_fmt) == 0)
		return (int_val(sbuf, timestamp, p));
	else {
		char	 buf[1024];

		strftime(buf, sizeof(buf), sbuf_data(p->item_fmt),
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
static inline struct sbuf *
format_home_url(struct sbuf *sbuf, struct pkg *pkg,
		struct percent_esc *p)
{
	char	*url;

	pkg_get(pkg, PKG_WWW, &url);
	return (string_val(sbuf, url, p));
}

/*
 * %% -- Output a literal '%' character
 */
static struct sbuf *
format_literal_percent(struct sbuf *sbuf, __unused struct pkg *pkg,
		       __unused struct percent_esc *p)
{
	sbuf_putc(sbuf, '%');
	return (sbuf);
}

static const char *
parse_escape(const char *f, unsigned context, struct percent_esc *p)
{
	bool		done = false;
	fmt_code_t	fmt_code;

	f++;			/* Eat the % */

	/* Field modifiers, if any:
	   '#' alternate form
	   '-' left align
	   '+' explicit plus sign (numerics only)
	   ' ' space instead of plus sign (numerics only)
	   '0' pad with zeroes (numerics only)
           '\'' use thousands separator (numerics only)
	   Note '*' (dynamic field width) is not supported */

	while (!done) {
		switch (*f) {
		case '#':
			p->flags |= PP_ALTERNATE_FORM1;
			break;
		case '?':
			p->flags |= PP_ALTERNATE_FORM2;
			break;
		case '-':
			p->flags |= PP_LEFT_ALIGN;
			break;
		case '+':
			p->flags |= PP_EXPLICIT_PLUS;
			break;
		case ' ':
			p->flags |= PP_SPACE_FOR_PLUS;
			break;
		case '0':
			p->flags |= PP_ZERO_PAD;
			break;
		case '\'':
			p->flags |= PP_THOUSANDS_SEP;
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
	   result on output is exactly the same. */

	done = false;
	while (!done) {
		switch(*f) {
		case '0':
			p->width = p->width * 10 + 0;
			break;
		case '1':
			p->width = p->width * 10 + 1;
			break;
		case '2':
			p->width = p->width * 10 + 2;
			break;
		case '3':
			p->width = p->width * 10 + 3;
			break;
		case '4':
			p->width = p->width * 10 + 4;
			break;
		case '5':
			p->width = p->width * 10 + 5;
			break;
		case '6':
			p->width = p->width * 10 + 6;
			break;
		case '7':
			p->width = p->width * 10 + 7;
			break;
		case '8':
			p->width = p->width * 10 + 8;
			break;
		case '9':
			p->width = p->width * 10 + 9;
			break;
		default:
			done = true;
			break;
		}
		if (!done)
			f++;
	}

	/* The next character or two will be a format code -- look
	   these up in the fmt table to make sure they are allowed in
	   context.  This could be optimized (but you know what they
	   say about premature optimization) since the format codes
	   are arranged alphabetically in the fmt[] array. */

	done = false;
	for (fmt_code = PP_PKG_SHLIBS; fmt_code < PP_END_MARKER; fmt_code++) {
		if ((fmt[fmt_code].context & context) == context &&
		    fmt[fmt_code].fmt_main == *f &&
		    (fmt[fmt_code].fmt_sub == '\0' ||
		     fmt[fmt_code].fmt_sub == f[1]))	{
			p->fmt_code = fmt_code;
			done = true;
			break;
		}
	}

	/* Not a recognised format code -- mark for pass through */

	if (!done) {
		p->fmt_code = PP_END_MARKER;
		return (f);	/* Caller will rewind */
	}

	/* Does this format take a trailing list item/separator format
	   like %{...%|...%} ?  It's only the list-valued items that
	   do, and they are *only* valid in PP_PKG context.  Also,
	   they only take the trailing stuff in the absence of %?X or
	   %#X modifiers. */

	if (fmt[p->fmt_code].context != PP_PKG ||
	    (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2)) != 0)
		return (f);

	/* ... and is the trailer present if so? */

	if (f[0] == '%' && f[1] == '{') {
		const char	*f2;
		bool		 item = false;
		bool		 sep = false;

		for (f2 = f + 2; *f2 != '\0'; f2++) {
			if (f2[0] == '%' && ( f2[1] == '}' || f2[1] == '|')) {
				if (f2[1] == '|')
					sep = true;
				break;
			}
			sbuf_putc(p->item_fmt, *f2);
		}
		if (item) {
			sbuf_finish(p->item_fmt);
			f = f2 + 1;
		
			if (sep) {
				sep = false;

				for (f2 = f; *f2 != '\0'; f2++) {
					if (f2[0] == '%' && f2[1] == '}') {
						sep = true;
						break;
					}
					sbuf_putc(p->sep_fmt, *f2);
				}

				if (sep) {
					sbuf_finish(p->sep_fmt);
					f = f2 + 1;
				} else {
					sbuf_clear(p->item_fmt);
					sbuf_clear(p->sep_fmt);
				}
			}
		} else {
			sbuf_clear(p->item_fmt);
			sbuf_clear(p->sep_fmt);
		}
	}

	return (f);
}

static const char *
process_format(struct sbuf *sbuf, const char *f, va_list ap)
{
	const char		*fstart;
	struct sbuf		*s;
	struct percent_esc	*p = new_percent_esc(NULL);
	void			*data;

	if (p == NULL)
		return (NULL);	/* Out of memory */

	fstart = f;
	f = parse_escape(f, PP_PKG, p);

	if (p->fmt_code <= PP_LAST_FORMAT)
		data = va_arg(ap, void *);
	else
		data = NULL;

	/* FFR.  Replace this monster switch statement by function
	 * pointers in fmt array. */
	switch (p->fmt_code) {
	case PP_PKG_SHLIBS:	/* list */
		s = format_shlibs(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_SHLIB_NAME:
		s = format_shlib_name(sbuf, (struct pkg_shlib *) data, p);
		break;
	case PP_PKG_CATEGORIES:	/* list */
		s = format_categories(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_CATEGORY_NAME:
		s = format_category_name(sbuf, (struct pkg_category *) data, p);
		break;
	case PP_PKG_DIRECTORIES: /* list */
		s = format_directories(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_DIRECTORY_GROUP:
		s = format_directory_group(sbuf, (struct pkg_dir *) data, p);
		break;
	case PP_PKG_DIRECTORY_KEEPFLAG:
		s = format_directory_keepflag(sbuf, (struct pkg_dir *) data, p);
		break;
	case PP_PKG_DIRECTORY_PATH:
		s = format_directory_path(sbuf, (struct pkg_dir *) data, p);
		break;
	case PP_PKG_DIRECTORY_PERMS:
		s = format_directory_perms(sbuf, (struct pkg_dir *) data, p);
		break;
	case PP_PKG_DIRECTORY_TRYFLAG:
		s = format_directory_tryflag(sbuf, (struct pkg_dir *) data, p);
		break;
	case PP_PKG_DIRECTORY_USER:
		s = format_directory_user(sbuf, (struct pkg_dir *) data, p);
		break;
	case PP_PKG_FILES:	/* list */
		s = format_files(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_FILE_GROUP:
		s = format_file_group(sbuf, (struct pkg_file *) data, p);
		break;
	case PP_PKG_FILE_KEEPFLAG:
		s = format_file_keepflag(sbuf, (struct pkg_file *) data, p);
		break;
	case PP_PKG_FILE_PATH:
		s = format_file_path(sbuf, (struct pkg_file *) data, p);
		break;
	case PP_PKG_FILE_PERMS:
		s = format_file_perms(sbuf, (struct pkg_file *) data, p);
		break;
	case PP_PKG_FILE_SHA256:
		s = format_file_sha256(sbuf, (struct pkg_file *) data, p);
		break;
	case PP_PKG_FILE_USER:
		s = format_file_user(sbuf, (struct pkg_file *) data, p);
		break;
	case PP_PKG_GROUPS:	/* list */
		s = format_groups(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_GROUP_GIDSTR:
		s = format_group_gidstr(sbuf, (struct pkg_group *) data, p);
		break;
	case PP_PKG_GROUP_NAME:
		s = format_group_name(sbuf, (struct pkg_group *) data, p);
		break;
	case PP_ROW_COUNTER:
		s = format_row_counter(sbuf, (int *) data, p);
		break;
	case PP_PKG_LICENSES:	/* list */
		s = format_licenses(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_LICENSE_NAME:
		s = format_license_name(sbuf, (struct pkg_license *) data, p);
		break;
	case PP_PKG_MESSAGE:
		s = format_message(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_OPTIONS:	/* list */
		s = format_options(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_OPTION_NAME:
		s = format_option_name(sbuf, (struct pkg_option *) data, p);
		break;
	case PP_PKG_OPTION_VALUE:
		s = format_option_value(sbuf, (struct pkg_option *) data, p);
		break;
	case PP_PKG_USERS:	/* list */
		s = format_users(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_USER_NAME:
		s = format_user_name(sbuf, (struct pkg_user *) data, p);
		break;
	case PP_PKG_USER_UIDSTR:
		s = format_user_uidstr(sbuf, (struct pkg_user *) data, p);
		break;
	case PP_PKG_AUTOREMOVE:
		s = format_autoremove(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_COMMENT:
		s = format_comment(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_DEPENDENCIES: /* list */
		s = format_dependencies(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_DEPENDENCY_NAME:
		s = format_dependency_name(sbuf, (struct pkg_dep *) data, p);
		break;
	case PP_PKG_DEPENDENCY_ORIGIN:
		s = format_dependency_origin(sbuf, (struct pkg_dep *) data, p);
		break;
	case PP_PKG_DEPENDENCY_VERSION:
		s = format_dependency_version(sbuf, (struct pkg_dep *) data, p);
		break;
	case PP_PKG_ADDITIONAL_INFO:
		s = format_add_info(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_LICENSE_LOGIC:
		s = format_license_logic(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_MAINTAINER:
		s = format_maintainer(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_NAME:
		s = format_name(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_ORIGIN:
		s = format_origin(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_PREFIX:
		s = format_prefix(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_REQUIREMENTS: /* list */
		s = format_requirements(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_REQUIREMENT_NAME: /* printing as dependency */
		s = format_dependency_name(sbuf, (struct pkg_dep *) data, p);
		break;
	case PP_PKG_REQUIREMENT_ORIGIN: /* printing as dependency */
		s = format_dependency_origin(sbuf, (struct pkg_dep *) data, p);
		break;
	case PP_PKG_REQUIREMENT_VERSION: /* printing as dependency */
		s = format_dependency_version(sbuf, (struct pkg_dep *) data, p);
		break;
	case PP_PKG_FLATSIZE:
		s = format_flatsize(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_INSTALL_TIMESTAMP:
		s = format_install_tstamp(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_VERSION:
		s = format_version(sbuf, (struct pkg *) data, p);
		break;
	case PP_PKG_HOME_PAGE:
		s = format_home_url(sbuf, (struct pkg *) data, p);
		break;
	case PP_LITERAL_PERCENT:
		s = format_literal_percent(sbuf, NULL, NULL);
		break;
	default:
		/* If it's not a known escape, pass through unchanged */
		sbuf_putc(sbuf, '%');
		s = NULL;
		break;
	}

	if (s == NULL)
		f = fstart;	/* Pass through unprocessed on error */

	free_percent_esc(p);

	return (f);
}

/**
 * print to stdout data from pkg as indicated by the format code format
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_printf(const char *format, ...)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	va_end(ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = printf("%s", sbuf_data(sbuf));
	} else
		count = -1;
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * print to named stream from pkg as indicated by the format code format
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters printed
 */
int
pkg_fprintf(FILE *stream, const char *format, ...)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	va_end(ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = fprintf(stream, "%s", sbuf_data(sbuf));
	} else
		count = -1;
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * print to file descriptor d data from pkg as indicated by the format
 * code format
 * @param d Previously opened file descriptor to print to
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_dprintf(int fd, const char *format, ...)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	va_end(ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = dprintf(fd, "%s", sbuf_data(sbuf));
	} else 
		count = -1;
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * print to buffer str of given size data from pkg as indicated by the
 * format code format as a NULL-terminated string
 * @param str Character array buffer to receive output
 * @param size Length of the buffer str
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters that would have been output
 * disregarding truncation to fit size
 */
int
pkg_snprintf(char *str, size_t size, const char *format, ...)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	va_end(ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = snprintf(str, size, "%s", sbuf_data(sbuf));
	} else
		count = -1;
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * Allocate a string buffer ret sufficiently big to contain formatted
 * data data from pkg as indicated by the format code format
 * @param ret location of pointer to be set to point to buffer containing
 * result 
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters printed
 */
int
pkg_asprintf(char **ret, const char *format, ...)
{
	struct sbuf	*sbuf = sbuf_new_auto();
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	va_end(ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = asprintf(ret, "%s", sbuf_data(sbuf));
	} else {
		count = -1;
		*ret = NULL;
	}
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * store data from pkg into sbuf as indicated by the format code format.
 * This is the core function called by all the other pkg_printf() family.
 * @param sbuf contains the result
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters in the result
 */
struct sbuf *
pkg_sbuf_printf(struct sbuf *sbuf, const char *format, ...)
{
	va_list		 ap;

	va_start(ap, format);
	sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	va_end(ap);

	return (sbuf);
}

/**
 * store data from pkg into sbuf as indicated by the format code format.
 * This is the core function called by all the other pkg_printf() family.
 * @param sbuf contains the result
 * @param ap Arglist with struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters in the result
 */
struct sbuf *
pkg_sbuf_vprintf(struct sbuf *sbuf, const char *format, va_list ap)
{
	const char	*f;

	for (f = format; *f != '\0'; f++) {
		if (*f == '%') {
			f = process_format(sbuf, f, ap);
		} else if (*f == '\\' ) {
			f = process_escape(sbuf, f);
		} else {
			sbuf_putc(sbuf, *f);
		}
		if (f == NULL) {
			sbuf_clear(sbuf);
			break;	/* Error: out of memory */
		}
	}
	return (sbuf);
}
/*
 * That's All Folks!
 */
