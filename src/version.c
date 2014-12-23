/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
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

#include <sys/param.h>
#include <sys/sbuf.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#define _WITH_GETLINE
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <pkg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <fnmatch.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <uthash.h>

#include "pkgcli.h"

extern char **environ;

struct index_entry {
	char *origin;
	char *version;
	UT_hash_handle hh;
};

struct port_entry {
	char *name;
	UT_hash_handle hh;
};

struct category {
	char *name;
	struct port_entry *ports;
	UT_hash_handle hh;
};

struct category *categories = NULL;

void
usage_version(void)
{
	fprintf(stderr, "Usage: pkg version [-IPR] [-hoqvU] [-l limchar] [-L limchar] [-Cegix pattern]\n");
	fprintf(stderr, "		    [-r reponame] [-O origin] [index]\n");
	fprintf(stderr, "	pkg version -t <version1> <version2>\n");
	fprintf(stderr, "	pkg version -T <pkgname> <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help version'.\n");
}

static void
print_version(struct pkg *pkg, const char *source, const char *ver,
	      char limchar, unsigned int opt)
{
	const char	*key;
	const char	*version;
	int		 cout;

	pkg_get(pkg, PKG_VERSION, &version);
	if (ver == NULL) {
		if (source == NULL)
			key = "!";
		else
			key = "?";
	} else {
		switch (pkg_version_cmp(version, ver)) {
		case -1:
			key = "<";
			break;
		case 0:
			key = "=";
			break;
		case 1:
			key = ">";
			break;
		default:
			key = "!";
			break;
		}
	}

	if ((opt & VERSION_STATUS) && limchar != *key)
		return;

	if ((opt & VERSION_NOSTATUS) && limchar == *key)
		return;

	if (opt & VERSION_ORIGIN)
		pkg_printf("%-34o %S", pkg, key);
	else {
		cout = pkg_printf("%n-%v", pkg, pkg);
		cout = 35 - cout;
		if (cout < 1)
			cout = 1;
		printf("%*s%s", cout, " ", key);
	}

	if (opt & VERSION_VERBOSE) {
		switch (*key) {
		case '<':
			printf("   needs updating (%s has %s)", source, ver);
			break;
		case '=':
			printf("   up-to-date with %s", source);
			break;
		case '>':
			printf("   succeeds %s (%s has %s)", source, source, ver);
			break;
		case '?':
			pkg_printf("   orphaned: %o", pkg);
			break;
		case '!':
			printf("   Comparison failed");
			break;
		}
	}

	printf("\n");
	return;
}

static int
do_testversion(unsigned int opt, int argc, char ** restrict argv)
{
	/* -t must be unique and takes two arguments */
	if ( opt != VERSION_TESTVERSION || argc < 2 ) {
		usage_version();
		return (EX_USAGE);
	}

	switch (pkg_version_cmp(argv[0], argv[1])) {
	case -1:
		printf("<\n");
		break;
	case 0:
		printf("=\n");
		break;
	case 1:
		printf(">\n");
		break;
	}

	return (EX_OK);
}

static int
do_testpattern(unsigned int opt, int argc, char ** restrict argv)
{
	bool	 pattern_from_stdin = false;
	bool	 pkgname_from_stdin = false;
	char	*line = NULL;
	size_t	 linecap = 0;
	ssize_t	 linelen;
	int	 retval = FNM_NOMATCH;

	/* -T must be unique and takes two arguments */
	if ( opt != VERSION_TESTPATTERN || argc < 2 ) {
		usage_version();
		return (EX_USAGE);
	}

	if (strncmp(argv[0], "-", 1) == 0)
		pattern_from_stdin = true;

	if (strncmp(argv[1], "-", 1) == 0)
		pkgname_from_stdin = true;

	if (pattern_from_stdin && pkgname_from_stdin) {
		usage_version();
		return (EX_USAGE);
	}

	if (!pattern_from_stdin && !pkgname_from_stdin)
		return (fnmatch(argv[1], argv[0], 0));

	while ((linelen = getline(&line, &linecap, stdin)) > 0) {
		line[linelen - 1] = '\0'; /* Strip trailing newline */

		if ((pattern_from_stdin && (fnmatch(argv[1], line, 0) == 0)) ||
		    (pkgname_from_stdin && (fnmatch(line, argv[0], 0) == 0))) {
			retval = EPKG_OK;
			printf("%.*s\n", (int)linelen, line);
		}
	}

	free(line);

	return (retval);
}

static bool
have_ports(const char **portsdir, bool show_error)
{
	char		 portsdirmakefile[MAXPATHLEN];
	struct stat	 sb;
	bool		 have_ports;

	/* Look for Makefile within $PORTSDIR as indicative of
	 * installed ports tree. */

	*portsdir = pkg_object_string(pkg_config_get("PORTSDIR"));
	if (*portsdir == NULL)
		err(1, "Cannot get portsdir config entry!");

	snprintf(portsdirmakefile, sizeof(portsdirmakefile),
		 "%s/Makefile", *portsdir);

	have_ports = (stat(portsdirmakefile, &sb) == 0 && S_ISREG(sb.st_mode));

	if (show_error && !have_ports)
		warnx("Cannot find ports tree: unable to open %s",
		      portsdirmakefile);

	return (have_ports);
}

static const char*
indexfilename(char *filebuf, size_t filebuflen)
{
	const char	*indexdir;
	const char	*indexfile;

	/* Construct the canonical name of the indexfile from the
	 * ports directory and the major version number of the OS.
	 * Overridden by INDEXDIR and INDEXFILE if defined. (Mimics
	 * the behaviour of ${PORTSDIR}/Makefile) */

	indexdir = pkg_object_string(pkg_config_get("INDEXDIR"));
	if (indexdir == NULL) {
		indexdir = pkg_object_string(pkg_config_get("PORTSDIR"));

		if (indexdir == NULL)
			err(EX_SOFTWARE, "Cannot get either INDEXDIR or "
			    "PORTSDIR config entry!");
	}

	indexfile = pkg_object_string(pkg_config_get("INDEXFILE"));
	if (indexfile == NULL)
		err(EX_SOFTWARE, "Cannot get INDEXFILE config entry!");

	strlcpy(filebuf, indexdir, filebuflen);

	if (filebuf[0] != '\0' && filebuf[strlen(filebuf) - 1] != '/')
		strlcat(filebuf, "/", filebuflen);

	strlcat(filebuf, indexfile, filebuflen);

	return (filebuf);
}

static struct index_entry *
hash_indexfile(const char *indexfilename)
{
	FILE			*indexfile;
	struct index_entry	*indexhead = NULL;
	struct index_entry	*entry;
	char			*version, *origin;
	char			*line = NULL, *l, *p;
	size_t			 linecap = 0;
	int			 dirs;


	/* Create a hash table of all the package names and port
	 * directories from the index file. */

	indexfile = fopen(indexfilename, "r");
	if (!indexfile)
		err(EX_NOINPUT, "Unable to open %s", indexfilename);

	while (getline(&line, &linecap, indexfile) > 0) {
		/* line is pkgname|portdir|... */

		l = line;

		version = strsep(&l, "|");
		version = strrchr(version, '-');
		version[0] = '\0';
		version++;

		origin = strsep(&l, "|");
		for (dirs = 0, p = l; p > origin; p--) {
			if ( p[-1] == '/' ) {
				dirs++;
				if (dirs == 2) {
					origin = p;
					break;
				}
			}
		}

		entry = malloc(sizeof(struct index_entry));
		if (entry != NULL) {
			entry->version = strdup(version);
			entry->origin = strdup(origin);
		}

		if (entry == NULL || entry->version == NULL ||
		    entry->origin == NULL)
			err(EX_SOFTWARE, "Out of memory while reading %s",
			    indexfilename);

		HASH_ADD_KEYPTR(hh, indexhead, entry->origin,
				strlen(entry->origin), entry);
	}

	free(line);
	fclose(indexfile);

	return (indexhead);
}

static void
free_port_entries(struct port_entry *entries)
{
	struct port_entry	*entry, *tmp;

	HASH_ITER(hh, entries, entry, tmp) {
		HASH_DEL(entries, entry);
		free(entry->name);
		free(entry);
	}
	return;
}

static void
free_categories(void)
{
	struct category	*cat, *tmp;

	HASH_ITER(hh, categories, cat, tmp) {
		HASH_DEL(categories, cat);
		free_port_entries(cat->ports);
		free(cat->name);
		free(cat);
	}
	return;
}

static void
free_index(struct index_entry *indexhead)
{
	struct index_entry	*entry, *tmp;

	HASH_ITER(hh, indexhead, entry, tmp) {
		HASH_DEL(indexhead, entry);
		free(entry->origin);
		free(entry->version);
		free(entry);
	}
	return;
}

static bool
have_indexfile(const char **indexfile, char *filebuf, size_t filebuflen,
	       int argc, char ** restrict argv, bool show_error)
{
	bool		have_indexfile = true;
	struct stat	sb;

	/* If there is a remaining command line argument, take
	   that as the name of the INDEX file to use.  Otherwise,
	   search for INDEX-N within the ports tree */

	if (argc == 0)
		*indexfile = indexfilename(filebuf, filebuflen);
	else
		*indexfile = argv[0];

	if (stat(*indexfile, &sb) == -1)
		have_indexfile = false;

	if (show_error && !have_indexfile)
		warn("Can't access %s", *indexfile);
	
	return (have_indexfile);
}

static int
do_source_index(unsigned int opt, char limchar, char *pattern, match_t match,
	        const char *matchorigin, const char *indexfile)
{
	struct index_entry	*indexhead;
	struct index_entry	*entry;
	struct pkgdb		*db = NULL;
	struct pkgdb_it		*it = NULL;
	struct pkg		*pkg = NULL;
	const char		*origin;

	if ( (opt & VERSION_SOURCES) != VERSION_SOURCE_INDEX) {
		usage_version();
		return (EX_USAGE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	indexhead = hash_indexfile(indexfile);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		free_index(indexhead);
		warnx("Cannot get a read lock on the database. "
		      "It is locked by another process");
		return (EX_TEMPFAIL);
	}

	it = pkgdb_query(db, pattern, match);
	if (it == NULL)
		goto cleanup;

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin);

		/* If -O was specified, check if this origin matches */

		if ((opt & VERSION_WITHORIGIN) &&
		    strcmp(origin, matchorigin) != 0)
			continue;
		
		HASH_FIND_STR(indexhead, origin, entry);
		print_version(pkg, "index",
		    entry != NULL ? entry->version : NULL, limchar, opt);
	}

cleanup:
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	free_index(indexhead);
	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (EPKG_OK);
}

static int
do_source_remote(unsigned int opt, char limchar, char *pattern, match_t match,
		 bool auto_update, const char *reponame,
		 const char *matchorigin)
{
	struct pkgdb	*db = NULL;
	struct pkgdb_it	*it = NULL;
	struct pkgdb_it	*it_remote = NULL;
	struct pkg	*pkg = NULL;
	struct pkg	*pkg_remote = NULL;
	const char	*origin;
	const char	*version_remote;

	int		 retcode = EPKG_OK;

	if ( (opt & VERSION_SOURCES) != VERSION_SOURCE_REMOTE ) {
		usage_version();
		return (EX_USAGE);
	}

	/* Only force remote mode if looking up remote, otherwise
	   user is forced to have a repo.sqlite */

	if (auto_update) {
		retcode = pkgcli_update(false, false, reponame);
		if (retcode != EPKG_OK)
			return (retcode);
	}

	if (pkgdb_open_all(&db, PKGDB_REMOTE, reponame) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database. "
		      "It is locked by another process");
		return (EX_TEMPFAIL);
	}

	it = pkgdb_query(db, pattern, match);
	if (it == NULL) {
		retcode = EX_IOERR;
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin);

		/* If -O was specified, check if this origin matches */
		if ((opt & VERSION_WITHORIGIN) &&
		    strcmp(origin, matchorigin) != 0)
			continue;

		it_remote = pkgdb_repo_query(db, origin, MATCH_EXACT, reponame);
		if (it_remote == NULL) {
			retcode = EX_IOERR;
			goto cleanup;
		}

		if (pkgdb_it_next(it_remote, &pkg_remote, PKG_LOAD_BASIC)
		    == EPKG_OK) {
			pkg_get(pkg_remote, PKG_VERSION, &version_remote);
			print_version(pkg, "remote", version_remote, limchar,
			    opt);
		} else {
			print_version(pkg, "remote", NULL, limchar, opt);
		}
		pkgdb_it_free(it_remote);
	}

cleanup:
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);

	pkg_free(pkg);
	pkg_free(pkg_remote);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (retcode);
}

static int
exec_buf(struct sbuf *res, char **argv) {
	char buf[BUFSIZ];
	int spawn_err;
	pid_t pid;
	int pfd[2];
	int r, pstat;
	posix_spawn_file_actions_t actions;

	if (pipe(pfd) < 0) {
		warn("pipe()");
		return (0);
	}

	if ((spawn_err = posix_spawn_file_actions_init(&actions)) != 0) {
		warnc(spawn_err, "%s", argv[0]);
		return (0);
	}

	if ((spawn_err = posix_spawn_file_actions_addopen(&actions,
	    STDERR_FILENO, "/dev/null", O_RDWR, 0)) != 0 ||
	    (spawn_err = posix_spawn_file_actions_addopen(&actions,
	    STDIN_FILENO, "/dev/null", O_RDONLY, 0)) != 0 ||
	    (spawn_err = posix_spawn_file_actions_adddup2(&actions,
	    pfd[1], STDOUT_FILENO)!= 0) ||
	    (spawn_err = posix_spawnp(&pid, argv[0], &actions, NULL,
	    argv, environ)) != 0) {
		posix_spawn_file_actions_destroy(&actions);
		warnc(spawn_err, "%s", argv[0]);
		return (0);
	}
	posix_spawn_file_actions_destroy(&actions);

	close(pfd[1]);

	sbuf_clear(res);
	while ((r = read(pfd[0], buf, BUFSIZ)) > 0)
		sbuf_bcat(res, buf, r);

	close(pfd[0]);
	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (-1);
	}

	sbuf_finish(res);

	return (sbuf_len(res));
}

static struct category *
category_new(char *categorypath, const char *category)
{
	struct sbuf		*makecmd;
	struct port_entry	*port;
	struct category		*cat = NULL;
	char			*results, *d;
	char			*argv[5];

	makecmd = sbuf_new_auto();

	argv[0] = "make";
	argv[1] = "-C";
	argv[2] = categorypath;
	argv[3] = "-VSUBDIR";
	argv[4] = NULL;

	if (exec_buf(makecmd, argv) <= 0)
		goto cleanup;

	results = sbuf_data(makecmd);

	cat = calloc(1, sizeof(struct category));
	cat->name = strdup(category);
	while ((d = strsep(&results, " \n")) != NULL) {
		port = calloc(1, sizeof(struct port_entry));
		port->name = strdup(d);
		HASH_ADD_KEYPTR(hh, cat->ports, port->name,
				strlen(port->name), port);
	}

	HASH_ADD_KEYPTR(hh, categories, cat->name,
			strlen(cat->name), cat);

cleanup:
	sbuf_delete(makecmd);

	return (cat);
}

static bool
validate_origin(const char *portsdir, const char *origin)
{
	char			*category, *buf;
	struct category		*cat;
	struct port_entry	*port;
	char			categorypath[MAXPATHLEN];

	snprintf(categorypath, MAXPATHLEN, "%s/%s", portsdir, origin);

	buf = strrchr(categorypath, '/');
	buf[0] = '\0';
	category = strrchr(categorypath, '/');
	category++;

	HASH_FIND_STR(categories, category, cat);
	if (cat == NULL) {
		cat = category_new(categorypath, category);
	}

	if (cat == NULL)
		return (false);

	buf = strrchr(origin, '/');
	buf++;

	HASH_FIND_STR(cat->ports, buf, port);

	return (port != NULL);
}

static const char *
port_version(struct sbuf *cmd, const char *portsdir, const char *origin)
{
	char	*output;
	char	*version = NULL;
	char	*argv[5];

	/* Validate the port origin -- check the SUBDIR settings
	   in the ports and category Makefiles, then extract the
	   version from the port itself. */

	if (validate_origin(portsdir, origin)) {
		sbuf_printf(cmd, "%s/%s", portsdir, origin);
		sbuf_finish(cmd);

		argv[0] = "make";
		argv[1] = "-C";
		argv[2] = sbuf_data(cmd);
		argv[3] = "-VPKGVERSION";
		argv[4] = NULL;

		if (exec_buf(cmd, argv) != 0) {
			output = sbuf_data(cmd);
			version = strsep(&output, "\n");
		}
	}
	return (version);
}

static int
do_source_ports(unsigned int opt, char limchar, char *pattern, match_t match,
		const char *matchorigin, const char *portsdir)
{
	struct pkgdb	*db = NULL;
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	struct sbuf	*cmd;
	const char	*origin;
	const char	*version;

	if ( (opt & VERSION_SOURCES) != VERSION_SOURCE_PORTS ) {
		usage_version();
		return (EX_USAGE);
	}


	if (chdir(portsdir) != 0)
		err(EX_SOFTWARE, "Cannot chdir to %s\n", portsdir); 

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database. "
		      "It is locked by another process");
		return (EX_TEMPFAIL);
	}

	if ((it = pkgdb_query(db, pattern, match)) == NULL)
			goto cleanup;

	cmd = sbuf_new_auto();

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin);

		/* If -O was specified, check if this origin matches */
		if ((opt & VERSION_WITHORIGIN) &&
		    strcmp(origin, matchorigin) != 0)
			continue;

		version = port_version(cmd, portsdir, origin);
		print_version(pkg, "port", version, limchar, opt);
		sbuf_clear(cmd);
	}

	sbuf_delete(cmd);

cleanup:
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);

	free_categories();
	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (EPKG_OK);
}

int
exec_version(int argc, char **argv)
{
	unsigned int	 opt = 0;
	char		 limchar = '-';
	const char	*matchorigin = NULL;
	const char	*reponame = NULL;
	const char	*portsdir;
	const char	*indexfile;
	const char	*versionsource;
	char		 filebuf[MAXPATHLEN];
	match_t		 match = MATCH_ALL;
	char		*pattern = NULL;
	int		 ch;

	struct option longopts[] = {
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "exact",		required_argument,	NULL,	'e' },
		{ "glob",		required_argument,	NULL,	'g' },
		{ "help",		no_argument,		NULL,	'h' },
		{ "index",		no_argument,		NULL,	'I' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "not-like",		required_argument,	NULL,	'L' },
		{ "like",		required_argument,	NULL,	'l' },
		{ "match-origin",	required_argument,	NULL,	'O' },
		{ "origin",		no_argument,		NULL,	'o' },
		{ "ports",		no_argument,		NULL,	'P' },
		{ "quiet",		no_argument,		NULL,	'q' },
		{ "remote",		no_argument,		NULL,	'R' },
		{ "repository",		required_argument,	NULL,	'r' },
		{ "test-pattern",	no_argument,		NULL,	'T' },
		{ "test-version",	no_argument,		NULL,	't' },
		{ "no-repo-update",	no_argument,		NULL,	'U' },
		{ "verbose",		no_argument,		NULL,	'v' },
		{ "regex",		required_argument,	NULL,	'x' },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+Ce:g:hIiL:l:O:oPqRr:TtUvx:",
				 longopts, NULL)) != -1) {
		switch (ch) {
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'e':
			match = MATCH_EXACT;
			pattern = optarg;
			break;
		case 'g':
			match = MATCH_GLOB;
			pattern = optarg;
			break;
		case 'h':
			usage_version();
			return (EX_OK);
		case 'I':
			opt |= VERSION_SOURCE_INDEX;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'L':
			opt |= VERSION_NOSTATUS;
			limchar = *optarg;
			break;
		case 'l':
			opt |= VERSION_STATUS;
			limchar = *optarg;
			break;
		case 'O':
			opt |= VERSION_WITHORIGIN;
			matchorigin = optarg;
			break;
		case 'o':
			opt |= VERSION_ORIGIN;
			break;
		case 'P':
			opt |= VERSION_SOURCE_PORTS;
			break;
		case 'q':
			opt |= VERSION_QUIET;
			break;
		case 'R':
			opt |= VERSION_SOURCE_REMOTE;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'T':
			opt |= VERSION_TESTPATTERN;
			break;
		case 't':
			opt |= VERSION_TESTVERSION;
			break;
		case 'U':
			auto_update = false;
			break;
		case 'v':
			opt |= VERSION_VERBOSE;
			break;
		case 'x':
			match = MATCH_REGEX;
			pattern = optarg;
			break;
		default:
			usage_version();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	/*
	 * Allowed option combinations:
	 *   -t ver1 ver2	 -- only
	 *   -T pkgname pattern	 -- only
	 *   Only one of -I -P -R can be given
	 */

	if ( (opt & VERSION_TESTVERSION) == VERSION_TESTVERSION )
		return (do_testversion(opt, argc, argv));

	if ( (opt & VERSION_TESTPATTERN) == VERSION_TESTPATTERN )
		return (do_testpattern(opt, argc, argv));

	if (opt & (VERSION_STATUS|VERSION_NOSTATUS)) {
		if (limchar != '<' &&
		    limchar != '>' &&
		    limchar != '=' &&
		    limchar != '?' &&
		    limchar != '!') {
			usage_version();
			return (EX_USAGE);
		}
	}

	if (argc > 1) {
		usage_version();
		return (EX_USAGE);
	}

	if ( !(opt & VERSION_SOURCES ) ) {
		versionsource = pkg_object_string(
		    pkg_config_get("VERSION_SOURCE"));
		if (versionsource != NULL) {
			switch (versionsource[0]) {
			case 'I':
				opt |= VERSION_SOURCE_INDEX;
				break;
			case 'P':
				opt |= VERSION_SOURCE_PORTS;
				break;
			case 'R':
				opt |= VERSION_SOURCE_REMOTE;
				break;
			default:
				warnx("Invalid VERSION_SOURCE"
				    " in configuration.");
			}
		}
	}

	if ( (opt & VERSION_SOURCE_INDEX) == VERSION_SOURCE_INDEX ) {
		if (!have_indexfile(&indexfile, filebuf, sizeof(filebuf),
		     argc, argv, true))
			return (EX_SOFTWARE);
		else
			return (do_source_index(opt, limchar, pattern, match,
				    matchorigin, indexfile));
	}

	if ( (opt & VERSION_SOURCE_REMOTE) == VERSION_SOURCE_REMOTE )
		return (do_source_remote(opt, limchar, pattern, match,
			    auto_update, reponame, matchorigin));

	if ( (opt & VERSION_SOURCE_PORTS) == VERSION_SOURCE_PORTS ) {
		if (!have_ports(&portsdir, true))
			return (EX_SOFTWARE);
		else
			return (do_source_ports(opt, limchar, pattern,
				    match, matchorigin, portsdir));
	}

	/* If none of -IPR were specified, and INDEX exists use that.
	   Failing that, if portsdir exists and is valid, use that
	   (slow) otherwise fallback to remote. */

	if (have_indexfile(&indexfile, filebuf, sizeof(filebuf), argc, argv,
            false)) {
		opt |= VERSION_SOURCE_INDEX;
		return (do_source_index(opt, limchar, pattern, match,
			    matchorigin, indexfile));
	} else if (have_ports(&portsdir, false)) {
		opt |= VERSION_SOURCE_PORTS;
		return (do_source_ports(opt, limchar, pattern, match,
			    matchorigin, portsdir));
	} else {
		opt |= VERSION_SOURCE_REMOTE;
		return (do_source_remote(opt, limchar, pattern, match,
			    auto_update, reponame, matchorigin));
	}

	/* NOTREACHED */
	return (EX_SOFTWARE);
}
/*
 * That's All Folks!
 */
