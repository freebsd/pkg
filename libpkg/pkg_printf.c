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

	count = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (count >= 0) {
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

	count = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (count >= 0) {
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

	count = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (count >= 0) {
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

	count = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (count >= 0) {
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

	count = pkg_sbuf_printf(sbuf, fmt, pkg);
	if (count >= 0) {
		sbuf_finish(sbuf);
		count = asprintf(ret, "%s", sbuf_data(sbuf));
	} else
		*ret = NULL;

	sbuf_delete(sbuf);
	return (count);
}

static const char*
maybe_read_hex_byte(struct sbuf *sbuf, const char *f, int *count)
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
		*count++;
	} else {
		/* Pass through unchanged if it's not a recognizable
		   hex byte. */
		sbuf_putc(sbuf, '\\');
		sbuf_putc(sbuf, 'x');
		*count += 2;
	}
	return (f);
}

static const char*
read_oct_byte(struct sbuf *sbuf, const char *f, int *count)
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
	*count++;

	return (f);
}

static const char *
process_escape(struct sbuf *sbuf, const char *f, int *count)
{
	f++;			/* Eat the \ */

	switch (*f) {
	case 'a':
		sbuf_putc(sbuf, '\a');
		*count++;
		break;
	case 'b':
		sbuf_putc(sbuf, '\b');
		*count++;
		break;
	case 'f':
		sbuf_putc(sbuf, '\f');
		*count++;
		break;
	case 'n':
		sbuf_putc(sbuf, '\n');
		*count++;
		break;
	case 't':
		sbuf_putc(sbuf, '\t');
		*count++;
		break;
	case 'v':
		sbuf_putc(sbuf, '\v');
		*count++;
		break;
	case '\'':
		sbuf_putc(sbuf, '\'');
		*count++;
		break;
	case '"':
		sbuf_putc(sbuf, '"');
		*count++;
	case '\\':
		sbuf_putc(sbuf, '\\');
		*count++;
		break;
	case 'x':		/* Hex escape: \xNN */
		f++;
		f = maybe_read_hex_byte(sbuf, f, count);
		break;
	case '0':		/* falls through */
	case '1':		/* falls through */
	case '2':		/* falls through */
	case '3':		/* falls through */
	case '4':		/* falls through */
	case '5':		/* falls through */
	case '6':		/* falls through */
	case '7':
		f = read_oct_byte(sbuf, f, count);
		break;
	default:		/* If it's not a recognised escape,
				   pass it through unchanged */
		sbuf_putc(sbuf, '\\');
		sbuf_putc(sbuf, *f);
		*count += 2;
		break;
	}

	return (f);
}

static const char *
process_format(struct sbuf *sbuf, const char *f, const struct pkg *pkg,
	       int *count)
{
	const char	*fstart;
	int		width = 0;
	bool		alternate_form = false;
	bool		left_align     = false;
	bool		explicit_plus  = false;
	bool		space_for_plus = false;
	bool		zero_pad       = false;
	bool		done           = false;

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
			alternate_form = true;
			break;
		case '-':
			left_align = true;
			break;
		case '+':
			explicit_plus = true;
			break;
		case ' ':
			space_for_plus = true;
			break;
		case '0':
			zero_pad = true;
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
	   zero padding. */

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

		/* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */

	case '%':
		sbuf_putc(sbuf, '%');
		*count++;
		break;
	default:
		/* If it's not a known escape, pass through unchanged */
		sbuf_putc(sbuf, '%');
		*count++;
		f = fstart;
		break;
	}

	return (f);
}


/**
 * store data from pkg into sbuf as indicated by the format code fmt.
 * This is the core function called by all the other pkg_printf() family.
 * @param sbuf contains the result
 * @param pkg The struct pkg supplying the data
 * @param fmt String with embedded %-escapes indicating what to output
 * @return count of the number of characters in the result
 */
int
pkg_sbuf_printf(struct sbuf *sbuf, const char *fmt, const struct pkg *pkg)
{
	int		 count = 0;
	const char	*f;


	for (f = fmt; *f != '\0'; f++) {
		if (*f == '%') {
			f = process_format(sbuf, f, pkg, &count);
		} else if (*f == '\\' ) {
			f = process_escape(sbuf, f, &count);
		} else {
			sbuf_putc(sbuf, *f);
			count++;
		}
	}
	return (count);
}
