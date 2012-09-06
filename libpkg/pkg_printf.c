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
#define _WITH_DPRINTF
#include <stdio.h>

#include "pkg.h"

#define PKG_PRINTF_ALTERNATE_FORM1	(1U << 0)
#define PKG_PRINTF_ALTERNATE_FORM2	(1U << 1)
#define PKG_PRINTF_LEFT_ALIGN		(1U << 2)
#define PKG_PRINTF_EXPLICIT_PLUS	(1U << 3)
#define PKG_PRINTF_SPACE_FOR_PLUS	(1U << 4)
#define PKG_PRINTF_ZERO_PAD		(1U << 5)


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
	int	i;

	/* Octal escapes are upto three octal digits: \N, \NN or \NNN
	   up to a max of \377 */

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
	case '0':		/* falls through */
	case '1':		/* falls through */
	case '2':		/* falls through */
	case '3':		/* falls through */
	case '4':		/* falls through */
	case '5':		/* falls through */
	case '6':		/* falls through */
	case '7':
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
static const char *
format_shlibs(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %C -- Categories.  List of Category names (strings). 1ary category is first.
 * Optionally accepts per-field format in %{ %| %}, where %c is replaced by the
 * category name.  Default %{%c%|, %}
 */
static const char *
format_categories(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %D -- Directories.  List of directory names (strings) possibly with
 * other meta-data.  Optionally accepts following per-field format in
 * %{ %| %}, where %d is replaced by the directory name.  Default
 * %{%d\n%|%}
 */
static const char *
format_directories(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %F -- Files.  List of filenames (strings) possibly with other meta-data.
 * Optionally accepts following per-field format in %{ %| %}, where
 * %f is replaced by the filename, %s by the checksum.  Default %{%f\n%|%}
 */
static const char *
format_files(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %G -- Groups. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %g will be replaced by each
 * groupname or %#g by the gid. Default %{%g\n%|%}
 */
static const char *
format_groups(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %L -- Licences. List of string values.  Optionally accepts
 * following per-field format in %{ %| %} where %L is replaced by the
 * license name and %l by the license logic.  Default %{%L%| %l %}
 */
static const char *
format_licenses(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %M -- Pkg message. string.  Accepts field-width, left-align
 */
static const char *
format_message(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %O -- Options. list of {option,value} tuples. Optionally accepts
 * following per-field format in %{ %| %}, where %k is replaced by the
 * option name and %v by the value.  Default %{%k %v\n%|%}
 */ 
static const char *
format_options(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %U -- Users. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %u will be replaced by each
 * username or %#g by the uid. Default %{%u\n%|%}
 */
static const char *
format_users(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %a -- Autoremove flag. boolean.  Accepts field-width, left-align.
 * Standard form: 0, 1.  Alternate form: no, yes
 */
static const char *
format_autoremove(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %c -- Comment. string.  Accepts field-width, left-align
 */
static const char *
format_comment(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %d -- Dependencies. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%n-%v\n" for each dependency.
 */
static const char *
format_dependencies(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %i -- Additional info. string. Accepts field-width, left-align
 */
static const char *
format_add_info(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %l -- Licence logic. string.  Accepts field-width, left-align.
 * Standard form: &, |, ''. Alternate form: and, or, ''.  
 */
static const char *
format_license_logic(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %m -- Maintainer e-mail address. string.  Accepts field-width, left-align
 */
static const char *
format_maintainer(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %n -- Package name. string.  Accepts field-width, left-align
 */
static const char *
format_name(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %o -- Package origin. string.  Accepts field-width, left-align
 */
static const char *
format_origin(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %p -- Installation prefix. string. Accepts field-width, left-align
 */
static const char *
format_prefix(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %r -- Requirements. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%{%n-%v\n%|%}" for each dependency.
 */
static const char *
format_requirements(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %s -- Size of installed package. integer.  Accepts field-width,
 * left-align, zero-fill, space-for-plus, explicit-plus and
 * alternate-form.  Alternate form is a humanized number using binary
 * scale prefixes (ki, Mi, etc.)
 */
static const char *
format_flatsize(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %t -- Installation timestamp (Unix time). integer.  Accepts field-width,
 * left-align.  Can be followed by optional strftime format string in
 * %{ %}.  Default is to print seconds-since-epoch as an integer, in
 * which case zero-fill, space-for-plus and explicit-plus also apply.
 */
static const char *
format_install_tstamp(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %v -- Package version. string. Accepts field width, left align
 */
static const char *
format_version(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}

/*
 * %w -- Home page URL.  string.  Accepts field width, left align
 */
static const char *
format_home_url(const char *f, const struct pkg *pkg, unsigned flags, int width)
{
	return (f);
}


static const char *
process_format(struct sbuf *sbuf, const char *f, const struct pkg *pkg)
{
	const char	*fstart;
	int		width = 0;
	unsigned	flags = 0;
	bool		done = false;

	fstart = f;

	f++;			/* Eat the % */

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
			flags |= PKG_PRINTF_ALTERNATE_FORM1;
			break;
		case '?':
			flags |= PKG_PRINTF_ALTERNATE_FORM2;
			break;
		case '-':
			flags |= PKG_PRINTF_LEFT_ALIGN;
			break;
		case '+':
			flags |= PKG_PRINTF_EXPLICIT_PLUS;
			break;
		case ' ':
			flags |= PKG_PRINTF_SPACE_FOR_PLUS;
			break;
		case '0':
			flags |= PKG_PRINTF_ZERO_PAD;
			break;
		default:
			done = true;
			break;
		}
		if (!done)
			f++;
	}

	/* Field width, if any -- some number of decimal digits.
	   Note: field width can't be set to zero, as that indicates
	   zero padding, so use width == 0 to flag "no explicit width
	   setting requested" */

	done = false;
	while (!done) {
		switch(*f) {
		case '0':
			width = width * 10 + 0;
			break;
		case '1':
			width = width * 10 + 1;
			break;
		case '2':
			width = width * 10 + 2;
			break;
		case '3':
			width = width * 10 + 3;
			break;
		case '4':
			width = width * 10 + 4;
			break;
		case '5':
			width = width * 10 + 5;
			break;
		case '6':
			width = width * 10 + 6;
			break;
		case '7':
			width = width * 10 + 7;
			break;
		case '8':
			width = width * 10 + 8;
			break;
		case '9':
			width = width * 10 + 9;
			break;
		default:
			done = true;
			break;
		}
		if (!done)
			f++;
	}

	/* Format code */
	switch (*f) {
	case '%':		/* literal % */
		sbuf_putc(sbuf, '%');
		break;
	case 'B':		/* shared libraries */
		f = format_shlibs(f, pkg, flags, width);
		break;
	case 'C':		/* categories */
		f = format_categories(f, pkg, flags, width);
		break;
	case 'D':		/* directories */
		f = format_directories(f, pkg, flags, width);
		break;
	case 'F':		/* files */
		f = format_files(f, pkg, flags, width);
		break;
	case 'G':		/* groups */
		f = format_groups(f, pkg, flags, width);
		break;
	case 'L':		/* licenses */
		f = format_licenses(f, pkg, flags, width);
		break;
	case 'M':		/* message */
		f = format_message(f, pkg, flags, width);
		break;
	case 'O':		/* options */
		f = format_options(f, pkg, flags, width);
		break;
	case 'U':		/* users */
		f = format_users(f, pkg, flags, width);
		break;
	case 'a':		/* autoremove flag */
		f = format_autoremove(f, pkg, flags, width);
		break;
	case 'c':		/* comment */
		f = format_comment(f, pkg, flags, width);
		break;
	case 'd':		/* dependencies */
		f = format_dependencies(f, pkg, flags, width);
		break;
	case 'i':		/* additional info */
		f = format_add_info(f, pkg, flags, width);
		break;
	case 'l':		/* license logic */
		f = format_license_logic(f, pkg, flags, width);
		break;
	case 'm':		/* maintainer */
		f = format_maintainer(f, pkg, flags, width);
		break;
	case 'n':		/* name */
		f = format_name(f, pkg, flags, width);
		break;
	case 'o':		/* origin */
		f = format_origin(f, pkg, flags, width);
		break;
	case 'p':		/* prefix */
		f = format_prefix(f, pkg, flags, width);
		break;
	case 'r':		/* requirements */
		f = format_requirements(f, pkg, flags, width);
		break;
	case 's':		/* flat size */
		f = format_flatsize(f, pkg, flags, width);
		break;
	case 't':		/* installation timestamp */
		f = format_install_tstamp(f, pkg, flags, width);
		break;
	case 'v':		/* version */
		f = format_version(f, pkg, flags, width);
		break;
	case 'w':		/* pkg home page URL */
		f = format_home_url(f, pkg, flags, width);
		break;
	default:
		/* If it's not a known escape, pass through unchanged */
		sbuf_putc(sbuf, '%');
		f = fstart;
		break;
	}

	return (f);
}

/**
 * print to stdout data from pkg as indicated by the format code fmt
 * @param pkg The struct pkg supplying the data
 * @param fmt String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_printf(const char *fmt, const struct pkg *pkg)
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
pkg_fprintf(FILE *stream, const char *fmt, const struct pkg *pkg)
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
pkg_dprintf(int fd, const char *fmt, const struct pkg *pkg)
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
pkg_snprintf(char *str, size_t size, const char *fmt, const struct pkg *pkg)
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
pkg_asprintf(char **ret, const char *fmt, const struct pkg *pkg)
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
struct sbuf *sbuf
pkg_sbuf_printf(struct sbuf *sbuf, const char *fmt, const struct pkg *pkg)
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
