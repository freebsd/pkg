/*-
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
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

#include <err.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

enum action {
	NONE,
	ADD,
	MODIFY,
	DELETE,
};

static 	bool yes;


void
usage_annotate(void)
{
	fprintf(stderr, "usage: pkg annotate [-giqxy] [-A|-M] <pkg-name> <key> [<value>]\n");
	fprintf(stderr, "       pkg annotate [-giqxy] -D <pkg-name> <key>\n");
	fprintf(stderr, "       pkg annotate [-qy] -a [-A|-M] <key> [<value>]\n");
	fprintf(stderr, "       pkg annotate [-qy] -a -D <key>\n");
	fprintf(stderr, "For more information see 'pkg help annotate'.\n");
}

static int
do_add(struct pkgdb *db, const char *pkgname, const char *pkgversion,
       const char *key, const char *value)
{
	int	 ret = EPKG_OK;

	if (yes || query_tty_yesno("%s-%s: Add annotation %s => %s? [y/N]: ",
			 pkgname, pkgversion, key, value)) {
		ret = pkgdb_add_annotation(db, pkgname, pkgversion,
                          key, value);
		if (ret == EPKG_OK) {
			if (!quiet)
				printf("Annotated %s-%s: %s\n", pkgname,
				     pkgversion, key);
		} else if (ret == EPKG_WARN) {
			if (!quiet)
				warnx("%s-%s: Can't add annotation %s -- "
				     "already exists", pkgname, pkgversion,
				     key );
		} else
			warnx("%s-%s: Failed to add annotation %s", pkgname,
			     pkgversion, key);
	}
	return (ret);
}

static int
do_modify(struct pkgdb *db, const char *pkgname, const char *pkgversion,
	  const char *key, const char *value)
{
	int	ret = EPKG_OK;

	if (yes || query_tty_yesno("%s-%s: Change %s annotation to "
		         "%s => %s? [y/N]: ",
			 pkgname, pkgversion, key, key, value)) {
		ret = pkgdb_modify_annotation(db, pkgname, pkgversion,
                          key, value);
		if (ret == EPKG_OK) {
			if (!quiet)
				printf("Modified annotation %s-%s: %s\n",
				       pkgname, pkgversion, key);
		} else
			warnx("%s-%s: Failed to modify annotation %s", pkgname,
			     pkgversion, key);
	}
	return (ret);
} 

static int
do_delete(struct pkgdb *db, const char *pkgname, const char *pkgversion,
	  const char *key)
{
	int	ret = EPKG_OK;

	if (yes || query_tty_yesno("%s-%s: Delete %s annotation [y/N]: ",
			 pkgname, pkgversion, key)) {
		ret = pkgdb_delete_annotation(db, pkgname, pkgversion, key);
		if (ret == EPKG_OK) {
			if (!quiet)
				printf("Deleted annotation %s-%s: %s\n",
				       pkgname, pkgversion, key);
		} else if (ret == EPKG_WARN) {
			if (!quiet)
				warnx("%s-%s: Can't delete annotation %s -- "
				     "nonexistent", pkgname, pkgversion, key);
		} else
			warnx("%s-%s: Failed to delete annotation %s", pkgname,
			     pkgversion, key);
	}
	return (ret);
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
	if (sbuf_finish(input) != 0)
		err(EX_DATAERR, "Could not read value data");

	return (input);
}

int
exec_annotate(int argc, char **argv)
{
	struct pkgdb	*db       = NULL;
	struct pkgdb_it	*it       = NULL;
	struct pkg	*pkg      = NULL;
	enum action	 action   = NONE;
	const char	*key;
	const char	*value;
	const char	*pkgname;
	const char	*pkgversion;
	struct sbuf	*input    = NULL;
	int		 ch;
	int		 match    = MATCH_EXACT;
	int		 retcode;
	int		 exitcode = EX_OK;

	pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);

	while ((ch = getopt(argc, argv, "aADgiMqxy")) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'A':
			action = ADD;
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
		key     = argv[0];
		value   = (argc > 1) ? argv[1] : NULL;
	} else {
		pkgname = argv[0];
		key     = argv[1];
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
		warnx("Insufficient privilege to modify package database");
		exitcode = EX_NOPERM;
		goto cleanup;
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing package database");
		exitcode = EX_SOFTWARE;
		goto cleanup;
	}

	retcode = pkgdb_open(&db, PKGDB_DEFAULT);
	if (retcode != EPKG_OK) {
		exitcode = EX_IOERR;
		goto cleanup;
	}

	if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
		exitcode = EX_IOERR;
		goto cleanup;
	}

	while ((retcode = pkgdb_it_next(it, &pkg, 0)) == EPKG_OK) {
		pkg_get(pkg, PKG_NAME, &pkgname, PKG_VERSION, &pkgversion);

		switch(action) {
		case NONE:	/* Should never happen */
			usage_annotate();
			exitcode = EX_USAGE;
			break;
		case ADD:
			retcode = do_add(db, pkgname, pkgversion, key, value);
			break;
		case MODIFY:
			retcode = do_modify(db, pkgname, pkgversion, key, value);
			break;
		case DELETE:
			retcode = do_delete(db, pkgname, pkgversion, key);
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
	if (pkg != NULL)
		pkg_free(pkg);
	if (it != NULL)
		pkgdb_it_free(it);
	if (db != NULL)
		pkgdb_close(db);
	if (input != NULL)
		sbuf_delete(input);

	return (exitcode);
}
