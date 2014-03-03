/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
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

#include <sys/param.h>
#include <sys/sbuf.h>
#include <sys/utsname.h>

#define _WITH_GETLINE
#include <err.h>
#include <pkg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <uthash.h>

#include "pkgcli.h"

struct index_entry {
	char *origin;
	char *version;
	UT_hash_handle hh;
};

void
usage_version(void)
{
	fprintf(stderr, "Usage: pkg version [-IPR] [-hoqvU] [-l limchar] [-L limchar] [-egix pattern]\n");
	fprintf(stderr, "                   [-r reponame] [-O origin] [index]\n");
	fprintf(stderr, "       pkg version -t <version1> <version2>\n");
	fprintf(stderr, "       pkg version -T <pkgname> <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help version'.\n");
}

static void
print_version(struct pkg *pkg, const char *source, const char *ver, char limchar, unsigned int opt)
{
	bool to_print = true;
	char key;
	const char *version;
	char *namever = NULL;

	pkg_get(pkg, PKG_VERSION, &version);
	if (ver == NULL) {
		if (source == NULL)
			key = '!';
		else
			key = '?';
	} else {
		switch (pkg_version_cmp(version, ver)) {
		case -1:
			key = '<';
			break;
		case 0:
			key = '=';
			break;
		case 1:
			key = '>';
			break;
		default:
			key = '!';
			break;
		}
	}

	if ((opt & VERSION_STATUS) && limchar != key) {
		to_print = false;
	}
	if ((opt & VERSION_NOSTATUS) && limchar == key) {
		to_print = false;
	}

	if (!to_print)
		return;


	if (opt & VERSION_ORIGIN)
		pkg_asprintf(&namever, "%-34o", pkg);
	else
		pkg_asprintf(&namever, "%n-%v", pkg, pkg);

	printf("%-34s %c", namever, key);
	free(namever);

	if (opt & VERSION_VERBOSE) {
		switch (key) {
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
}

int
exec_version(int argc, char **argv)
{
	unsigned int opt = 0;
	int ch;
	FILE *indexfile;
	char indexpath[MAXPATHLEN];
	struct index_entry *indexhead = NULL;
	struct utsname u;
	int rel_major_ver;
	int retval;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	char *buf;
	char *version;
	struct index_entry *entry, *tmp;
	struct pkgdb *db = NULL;
	struct pkg *pkg = NULL, *pkg_remote = NULL;
	struct pkgdb_it *it = NULL, *it_remote = NULL;
	char limchar = '-';
	struct sbuf *cmd;
	struct sbuf *res;
	const char *portsdir;
	const char *origin;
	const char *matchorigin = NULL;
	const char *reponame = NULL;
	const char *version_remote = NULL;
	bool have_ports;
	bool auto_update;
	match_t match = MATCH_ALL;
	char *pattern=NULL;
	struct stat sb;
	char portsdirmakefile[MAXPATHLEN];
	int retcode = EXIT_SUCCESS;

	pkg_config_bool(PKG_CONFIG_REPO_AUTOUPDATE, &auto_update);

	while ((ch = getopt(argc, argv, "hIPRUoqvl:L:ix:g:e:O:r:tT")) != -1) {
		switch (ch) {
		case 'h':
			usage_version();
			return (EX_OK);
		case 'I':
			opt |= VERSION_SOURCE_INDEX;
			break;
		case 'R':
			opt |= VERSION_SOURCE_REMOTE;
			break;
		case 'U':
			auto_update = false;
			break;
		case 'P':
			opt |= VERSION_SOURCE_PORTS;
			break;
		case 'o':
			opt |= VERSION_ORIGIN;
			break;
		case 'q':
			opt |= VERSION_QUIET;
			break;
		case 'v':
			opt |= VERSION_VERBOSE;
			break;
		case 'l':
			opt |= VERSION_STATUS;
			limchar = *optarg;
			break;
		case 'L':
			opt |= VERSION_NOSTATUS;
			limchar = *optarg;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'x':
			match = MATCH_REGEX;
			pattern = optarg;
			break;
		case 'g':
			match = MATCH_GLOB;
			pattern = optarg;
			break;
		case 'e':
			match = MATCH_EXACT;
			pattern = optarg;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'O':
			opt |= VERSION_WITHORIGIN;
			matchorigin = optarg;
			break;
		case 't':
			opt |= VERSION_TESTVERSION;
			break;
		case 'T':
			opt |= VERSION_TESTPATTERN;
			break;
		default:
			usage_version();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (opt & VERSION_STATUS) {
			if (limchar != '<' &&
					limchar != '>' &&
					limchar != '=') {
				usage_version();
				return (EX_USAGE);
			}
	}

	/* -t must be unique */
	if (((opt & VERSION_TESTVERSION) && opt != VERSION_TESTVERSION) ||
			(opt == VERSION_TESTVERSION && argc < 2)) {
		usage_version();
		return (EX_USAGE);
	
	} else if (opt == VERSION_TESTVERSION) {
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
	/* -T must be unique */
	} else if (((opt & VERSION_TESTPATTERN) && opt != VERSION_TESTPATTERN) ||
			(opt == VERSION_TESTPATTERN && argc != 2)) {
		usage_version();
		return (EX_USAGE);
	
	} else if (opt == VERSION_TESTPATTERN) {
		if (strcmp(argv[0], "-") == 0) {
			ch = 0; /* pattern from stdin */
		} else if (strcmp(argv[1], "-") == 0) {
			ch = 1; /* pkgname from stdin */
		} else return (fnmatch(argv[1], argv[0], 0));
		
		retval = FNM_NOMATCH;
		
		while ((linelen = getline(&line, &linecap, stdin)) > 0) {
			line[linelen - 1] = '\0'; /* Strip trailing newline */
			if ((ch == 0 && (fnmatch(argv[1], line, 0) == 0)) ||
				(ch == 1 && (fnmatch(line, argv[0], 0) == 0))) {
				retval = EPKG_OK;
				printf("%.*s\n", (int)linelen, line);
			}
		}

		free(line);
		
		return (retval);
		
	} else {
		if (pkg_config_string(PKG_CONFIG_PORTSDIR, &portsdir) != EPKG_OK)
			err(1, "Cannot get portsdir config entry!");

		snprintf(portsdirmakefile, sizeof(portsdirmakefile),
		    "%s/Makefile", portsdir);

		have_ports = (stat(portsdirmakefile, &sb) == 0 && S_ISREG(sb.st_mode));

		/* If none of -IPR were specified, and portsdir exists use that,
		   otherwise fallback to remote. */
		if ((opt & (VERSION_SOURCE_PORTS|VERSION_SOURCE_REMOTE|VERSION_SOURCE_INDEX)) == 0) {
			if (have_ports)
				opt |= VERSION_SOURCE_PORTS;
			else
				opt |= VERSION_SOURCE_REMOTE;
		}

		if (!have_ports && (opt & (VERSION_SOURCE_INDEX|VERSION_SOURCE_PORTS)))
			err(1, "Unable to open ports directory %s", portsdir);

		/* Only force remote mode if looking up remote, otherwise
		   user is forced to have a repo.sqlite */
		if (opt & VERSION_SOURCE_REMOTE) {
			if (auto_update) {

				retcode = pkgcli_update(false);
				if (retcode != EPKG_OK)
					return (retcode);
			}
			if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
				return (EX_IOERR);
		} else
			if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
				return (EX_IOERR);

		if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY, 0, 0) != EPKG_OK) {
			pkgdb_close(db);
			warnx("Cannot get a read lock on a database, it is locked by another process");
			return (EX_TEMPFAIL);
		}

		if ((it = pkgdb_query(db, pattern, match)) == NULL)
			goto cleanup;

		if (opt & VERSION_SOURCE_INDEX) {
			uname(&u);
			rel_major_ver = (int) strtol(u.release, NULL, 10);
			snprintf(indexpath, sizeof(indexpath), "%s/INDEX-%d", portsdir, rel_major_ver);
			indexfile = fopen(indexpath, "r");
			if (!indexfile) {
				warnx("Unable to open %s!", indexpath);
				retcode = EX_IOERR;
				goto cleanup;
			}

			while ((linelen = getline(&line, &linecap, indexfile)) > 0) {
				/* line is pkgname|portdir|... */
				buf = strchr(line, '|');
				buf[0] = '\0';
				buf++;
				version = strrchr(line, '-');
				version[0] = '\0';
				version++;
				buf = strchr(buf, '|');
				buf[0] = '\0';
				buf--;
				/* go backward to get the last two dirs of portsdir */
				while (buf[0] != '/')
					buf--;
				buf--;
				while (buf[0] != '/')
					buf--;
				buf++;

				entry = malloc(sizeof(struct index_entry));
				entry->version = strdup(version);
				entry->origin = strdup(buf);
				HASH_ADD_KEYPTR(hh, indexhead, entry->origin, strlen(entry->origin), entry);
			}
			free(line);
			fclose(indexfile);
		}

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			pkg_get(pkg, PKG_ORIGIN, &origin);

			/* If -O was specific, check if this origin matches */
			if ((opt & VERSION_WITHORIGIN) && strcmp(origin, matchorigin) != 0)
				continue;

			if (opt & VERSION_SOURCE_INDEX) {
				HASH_FIND_STR(indexhead, origin, entry);
				if (entry != NULL)
					print_version(pkg, "index", entry->version, limchar, opt);
			} else if (opt & VERSION_SOURCE_PORTS) {
				cmd = sbuf_new_auto();
				sbuf_printf(cmd, "make -C %s/%s -VPKGVERSION 2>/dev/null", portsdir, origin);
				sbuf_finish(cmd);

				if ((res = exec_buf(sbuf_data(cmd))) != NULL) {
					buf = sbuf_data(res);
					while (*buf != '\0') {
						if (*buf == '\n') {
							*buf = '\0';
							break;
						}
						buf++;
					}
					print_version(pkg, "port", sbuf_data(res), limchar, opt);
					sbuf_delete(res);
				} else {
					print_version(pkg, "port", NULL, limchar, opt);
				}
				sbuf_delete(cmd);
			} else if (opt & VERSION_SOURCE_REMOTE) {
				if ((it_remote = pkgdb_rquery(db, origin, MATCH_EXACT, reponame)) == NULL)
					return (EX_IOERR);
				if (pkgdb_it_next(it_remote, &pkg_remote, PKG_LOAD_BASIC) == EPKG_OK) {
					pkg_get(pkg_remote, PKG_VERSION, &version_remote);
					print_version(pkg, "remote", version_remote, limchar, opt);
				} else {
					print_version(pkg, "remote", NULL, limchar, opt);
				}
				pkgdb_it_free(it_remote);
			}
		}
	}
	
cleanup:
	HASH_ITER(hh, indexhead, entry, tmp) {
		HASH_DEL(indexhead, entry);
		free(entry->origin);
		free(entry->version);
		free(entry);
	}

	pkg_free(pkg);
	pkg_free(pkg_remote);
	pkgdb_it_free(it);
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (retcode);
}
