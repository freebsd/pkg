/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
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

#ifdef HAVE_CAPSICUM
#include <sys/capability.h>
#endif

#include <sys/queue.h>

#define _WITH_GETLINE
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <pkg.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pkgcli.h"

struct installed_ports {
	char *origin;
	SLIST_ENTRY(installed_ports) next;
};

void
usage_updating(void)
{
	fprintf(stderr, "Usage: pkg updating [-d YYYYMMDD] [-f file] [portname ...]\n");
	fprintf(stderr, "For more information see 'pkg help updating'.\n");

}

int
exec_updating(int argc, char **argv)
{
	char			*date = NULL;
	char			*dateline = NULL;
	char			*updatingfile = NULL;
	struct installed_ports	*port;
	SLIST_HEAD(,installed_ports) origins;
	int			 ch;
	char			*line = NULL;
	size_t			 linecap = 0;
	ssize_t			 linelen;
	char			*tmp;
	int			 head = 0;
	int			 found = 0;
	struct pkgdb		*db = NULL;
	struct pkg		*pkg = NULL;
	struct pkgdb_it		*it = NULL;
	const char		*origin;
	FILE			*fd;
	int			 retcode = EXIT_SUCCESS;
#ifdef HAVE_CAPSICUM
	cap_rights_t rights;
#endif

	struct option longopts[] = {
		{ "date",	required_argument,	NULL,	'd' },
		{ "file",	required_argument,	NULL,	'f' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+d:f:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'd':
			date = optarg;
			break;
		case 'f':
			updatingfile = optarg;
			break;
		default:
			usage_updating();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	/* checking date format */
	if (date != NULL)
		if (strlen(date) != 8 || strspn(date, "0123456789") != 8)
			err(EX_DATAERR, "Invalid date format");

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if (updatingfile == NULL) {
		const char *portsdir = pkg_object_string(pkg_config_get("PORTSDIR"));
		if (portsdir == NULL) {
			retcode = EX_CONFIG;
			goto cleanup;
		}
		asprintf(&updatingfile, "%s/UPDATING", portsdir);
	}

	fd = fopen(updatingfile, "r");
	if (fd == NULL) {
		warnx("Unable to open: %s", updatingfile);
		goto cleanup;
	}

#ifdef HAVE_CAPSICUM
	cap_rights_init(&rights, CAP_READ);
	if (cap_rights_limit(fileno(fd), &rights) < 0 && errno != ENOSYS ) {
		warn("cap_rights_limit() failed");
		return (EX_SOFTWARE);
	}

	if (cap_enter() < 0 && errno != ENOSYS) {
		warn("cap_enter() failed");
		return (EX_SOFTWARE);
	}
#endif

	SLIST_INIT(&origins);
	if (argc == 0) {
		if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL) {
			retcode = EX_UNAVAILABLE;
			goto cleanup;
		}

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			port = malloc(sizeof(struct installed_ports));
			pkg_get(pkg, PKG_ORIGIN, &origin);
			port->origin = strdup(origin);
			SLIST_INSERT_HEAD(&origins, port, next);
		}
	} else {
		while (*argv) {
			port = malloc(sizeof(struct installed_ports));
			port->origin = strdup(*argv);
			SLIST_INSERT_HEAD(&origins, port, next);
			argv++;
		}
	}

	while ((linelen = getline(&line, &linecap, fd)) > 0) {
		if (strspn(line, "0123456789:") == 9) {
			dateline = strdup(line);
			found = 0;
			head = 1;
		} else if (head == 0) {
			continue;
		}

		tmp = NULL;
		if (found == 0) {
			if (strstr(line, "AFFECTS") != NULL) {
				SLIST_FOREACH(port, &origins, next) {
					if ((tmp = strstr(line, port->origin)) != NULL) {
						break;
					}
				}
				if (tmp != NULL) {
					if ((date != NULL) && strncmp(dateline, date, 8) < 0)
						continue;
					printf("%s%s",dateline, line);
					found = 1;
				}
			}
		} else {
			printf("%s",line);
		}
	}
	fclose(fd);

cleanup:
	pkgdb_it_free(it);
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);
	pkg_free(pkg);

	return (retcode);
}
