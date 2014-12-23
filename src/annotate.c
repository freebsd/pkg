/*-
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/types.h>
#include <sys/sbuf.h>

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

enum action {
	NONE,
	ADD,
	MODIFY,
	DELETE,
	SHOW,
};

void
usage_annotate(void)
{
	fprintf(stderr,
            "Usage: pkg annotate [-Cgiqxy] [-A|M] <pkg-name> <tag> [<value>]\n");
	fprintf(stderr,
            "       pkg annotate [-Cgiqxy] [-S|D] <pkg-name> <tag>\n");
	fprintf(stderr,
            "       pkg annotate [-qy] -a [-A|M] <tag> [<value>]\n");
	fprintf(stderr,
            "       pkg annotate [-qy] -a [-S|D] <tag>\n");
	fprintf(stderr,
            "For more information see 'pkg help annotate'.\n");
}

static int
do_add(struct pkgdb *db, struct pkg *pkg, const char *tag, const char *value)
{
	int	ret = EPKG_OK;


	if (yes || query_tty_yesno(false, "%n-%v: Add annotation tagged: %S with "
	               "value: %S? [y/N]: ", pkg, pkg, tag, value)) {

		ret = pkgdb_add_annotation(db, pkg, tag, value);
		if (ret == EPKG_OK) {
			if (!quiet)
				pkg_printf("%n-%v: added annotation tagged:"
				    " %S\n", pkg, pkg, tag);
		} else if (ret == EPKG_WARN) {
			if (!quiet) {
				pkg_warnx("%n-%v: Cannot add annotation tagged:"
				    " %S\n", pkg, pkg, tag);
			}
		} else {
			pkg_warnx("%n-%v: Failed to add annotation tagged:"
			    " %S\n", pkg, pkg, tag);
		}
	}
	return (ret);
}

static int
do_modify(struct pkgdb *db, struct pkg *pkg, const char *tag, const char *value)
{
	int	ret = EPKG_OK;


	if (yes || query_tty_yesno(false, "%n-%v: Change annotation tagged: %S to "
		         "new value: %S? [y/N]: ", pkg, pkg, tag, value)) {
		ret = pkgdb_modify_annotation(db, pkg, tag, value);
		if (ret == EPKG_OK || ret == EPKG_WARN) {
			if (!quiet)
				pkg_printf("%n-%v: Modified annotation "
				       "tagged: %S\n", pkg, pkg, tag);
		} else {
			pkg_warnx("%n-%v: Failed to modify annotation tagged:"
			    " %S", pkg, pkg, tag);
		}
	}
	return (ret);
} 

static int
do_delete(struct pkgdb *db, struct pkg *pkg, const char *tag)
{
	int	ret = EPKG_OK;

	if (yes || query_tty_yesno(false, "%n-%v: Delete annotation tagged: %S? "
			 "[y/N]: ", pkg, pkg, tag)) {
		ret = pkgdb_delete_annotation(db, pkg, tag);
		if (ret == EPKG_OK) {
			if (!quiet)
				pkg_printf("%n-%v: Deleted annotation "
				       "tagged: %S\n", pkg, pkg, tag);
		} else if (ret == EPKG_WARN) {
			if (!quiet) {
				pkg_warnx("%n-%v: Cannot delete annotation "
				     "tagged: %S -- because there is none",
				     pkg, pkg, tag);
			}
		} else {
			pkg_warnx("%n-%v: Failed to delete annotation tagged: %S",
			     pkg, pkg, tag);
		}
	}
	return (ret);
} 

static int
do_show(struct pkg *pkg, const char *tag)
{
	struct pkg_kv *note;

	pkg_get(pkg, PKG_ANNOTATIONS, &note);
	while (note != NULL) {
		if (strcmp(tag, note->key) == 0) {
			if (quiet)
				printf("%s\n", note->value);
			else
				pkg_printf("%n-%v: Tag: %S Value: %S\n",
				    pkg, pkg, note->key, note->value);
			return (EPKG_OK);
		}
		note = note->next;
	}

	return (EPKG_FATAL);
}


static struct sbuf *
read_input(void)
{
	struct sbuf	*input;
	int		 ch;

	input = sbuf_new_auto();

	for (;;) {
		ch = getc(stdin);
		if (ch == EOF) {
			if (feof(stdin))
				break;
			if (ferror(stdin))
				err(EX_NOINPUT, "Failed to read stdin");
		}
		sbuf_putc(input, ch);
	}
#ifdef __DragonFly__
	sbuf_finish(input);
#else
	if (sbuf_finish(input) != 0)
		err(EX_DATAERR, "Could not read value data");
#endif

	return (input);
}

int
exec_annotate(int argc, char **argv)
{
	struct pkgdb	*db       = NULL;
	struct pkgdb_it	*it       = NULL;
	struct pkg	*pkg      = NULL;
	enum action	 action   = NONE;
	const char	*tag;
	const char	*value;
	const char	*pkgname;
	struct sbuf	*input    = NULL;
	int		 ch;
	int		 match    = MATCH_EXACT;
	int		 retcode;
	int		 exitcode = EX_OK;
	int		 flags = 0;

	struct option longopts[] = {
		{ "all",		no_argument,	NULL,	'a' },
		{ "add",		no_argument,	NULL,	'A' },
		{ "case-insensitive",	no_argument,	NULL,	'C' },
		{ "delete",		no_argument,	NULL,	'D' },
		{ "glob",		no_argument,	NULL,	'g' },
		{ "case-insensitive",	no_argument,	NULL,	'i' },
		{ "modify",		no_argument,	NULL,	'M' },
		{ "quiet",		no_argument,	NULL,	'q' },
		{ "show",		no_argument,	NULL,	'S' },
		{ "regex",		no_argument,	NULL,	'x' },
		{ "yes",		no_argument,	NULL,	'y' },
		{ NULL,			0,		NULL,	0   },
	};

        /* Set default case sensitivity for searching */
        pkgdb_set_case_sensitivity(
                pkg_object_bool(pkg_config_get("CASE_SENSITIVE_MATCH"))
                );

	while ((ch = getopt_long(argc, argv, "+aACDgiMqSxy", longopts, NULL))
	       != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'A':
			action = ADD;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'D':
			action = DELETE;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'M':
			action = MODIFY;
			break;
		case 'q':
			quiet = true;
			break;
		case 'S':
			action = SHOW;
			flags |= PKG_LOAD_ANNOTATIONS;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'y':
			yes = true;
			break;
		default:
			usage_annotate();
			return (EX_USAGE);
		}
        }
	argc -= optind;
	argv += optind;

	if (action == NONE || 
	    (match == MATCH_ALL && argc < 1) ||
	    (match != MATCH_ALL && argc < 2)) {
		usage_annotate();
		return (EX_USAGE);
	}

	if (match == MATCH_ALL) {
		pkgname = NULL;
		tag     = argv[0];
		value   = (argc > 1) ? argv[1] : NULL;
	} else {
		pkgname = argv[0];
		tag     = argv[1];
		value   = (argc > 2) ? argv[2] : NULL;
	}

	if ((action == ADD || action == MODIFY) && value == NULL) {
		/* try and read data for the value from stdin. */
		input = read_input();
		value = sbuf_data(input);
	}

	retcode = pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
			       PKGDB_DB_LOCAL);
	if (retcode == EPKG_ENODB) {
		if (match == MATCH_ALL) {
			exitcode = EX_OK;
			goto cleanup;
		}
		if (!quiet)
			warnx("No packages installed.  Nothing to do!");
		exitcode = EX_OK;
		goto cleanup;
	} else if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to modify the package database");
		exitcode = EX_NOPERM;
		goto cleanup;
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		exitcode = EX_SOFTWARE;
		goto cleanup;
	}

	retcode = pkgdb_open(&db, PKGDB_DEFAULT);
	if (retcode != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_EXCLUSIVE) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an exclusive lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
		exitcode = EX_IOERR;
		goto cleanup;
	}

	while ((retcode = pkgdb_it_next(it, &pkg, flags)) == EPKG_OK) {

		switch(action) {
		case NONE:	/* Should never happen */
			usage_annotate();
			exitcode = EX_USAGE;
			break;
		case ADD:
			retcode = do_add(db, pkg, tag, value);
			break;
		case MODIFY:
			retcode = do_modify(db, pkg, tag, value);
			break;
		case DELETE:
			retcode = do_delete(db, pkg, tag);
			break;
		case SHOW:
			retcode = do_show(pkg, tag);
			break;
		}

		if (retcode == EPKG_WARN)
			exitcode = EX_DATAERR;

		if (retcode != EPKG_OK && retcode != EPKG_WARN) {
			exitcode = EX_IOERR;
			goto cleanup;
		}
	}

cleanup:
	pkg_free(pkg);
	pkgdb_it_free(it);

	pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);
	pkgdb_close(db);
	if (input != NULL)
		sbuf_delete(input);

	return (exitcode);
}
