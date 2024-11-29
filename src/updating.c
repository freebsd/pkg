/*-
 * Copyright (c) 2011-2022 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <sys/capsicum.h>
#endif

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <pkg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <regex.h>
#include <tllist.h>

#include "pkgcli.h"

struct installed_ports {
	char *origin;
};

struct regex_cache {
	char *pattern;
	regex_t reg;
};

void
usage_updating(void)
{
	fprintf(stderr, "Usage: pkg updating [-i] [-d YYYYMMDD] [-f file] [portname ...]\n");
	fprintf(stderr, "For more information see 'pkg help updating'.\n");

}

static char *
convert_re(const char *src)
{
	const char *p;
	char *q;
	bool brace_flag = false;
	size_t len = strlen(src);
	char *buf = malloc(len*2+1);
	if (buf == NULL)
		return NULL;

	for (p=src, q=buf; p < src+len; p++) {
		switch (*p) {
		case '*':
			*q++ = '.';
			*q++ = '*';
			break;
		case '?':
			*q++ = '.';
			break;
		case '.':
			*q++ = '\\';
			*q++ = '.';
			break;
		case '{':
			*q++='(';
			brace_flag=true;
			break;
		case ',':
			if (brace_flag)
				*q++='|';
			else
				*q++=*p;
			break;
		case '}':
			*q++=')';
			brace_flag=false;
			break;
		default:
			*q++ = *p;
		}
	}
	*q ='\0';
	return buf;
}

int
matcher(const char *affects, const char *origin, bool ignorecase)
{
	int i, n, count, found, ret, rc;
	bool was_spc;
	size_t len;
	char *re, *buf, *p, **words;
	struct regex_cache *ent;
	tll(struct regex_cache *) cache = tll_init();

	len = strlen(affects);
	buf = strdup(affects);
	if (buf == NULL)
		return 0;

	for (count = 0, was_spc = true, p = buf; p < buf + len ; p++) {
		if (isspace(*p)) {
			if (!was_spc)
				was_spc = true;
			*p = '\0';
		} else {
			if (was_spc) {
				count++;
				was_spc = false;
			}
		}
	}

	words = malloc(sizeof(char*)*count);
	if (words == NULL) {
		free(buf);
		return 0;
	}

	for (i = 0, was_spc = true, p = buf; p < buf + len ; p++) {
		if (*p == '\0') {
			if (!was_spc)
				was_spc = true;
		} else {
			if (was_spc) {
				words[i++] = p;
				was_spc = false;
			}
		}
	}

	for(ret = 0, i = 0; i < count; i++) {
		n = strlen(words[i]);
		if (words[i][n-1] == ',') {
			words[i][n-1] = '\0';
		}
		if (strpbrk(words[i],"^$*|?") == NULL &&
			(strchr(words[i],'[') == NULL || strchr(words[i],']') == NULL) &&
			(strchr(words[i],'{') == NULL || strchr(words[i],'}') == NULL) &&
			(strchr(words[i],'(') == NULL || strchr(words[i],')') == NULL)) {
			if (ignorecase) {
				if (strcasecmp(words[i], origin) == 0) {
					ret = 1;
					break;
				}
			} else {
				if (strcmp(words[i], origin) == 0) {
					ret = 1;
					break;
				}
			}
			continue;
		}

		found = 0;
		tll_foreach(cache, it) {
			if (ignorecase)
				rc = strcasecmp(words[i], it->item->pattern);
			else
				rc = strcmp(words[i], it->item->pattern);
			if (rc == 0) {
				found++;
				if (regexec(&it->item->reg, origin, 0, NULL, 0) == 0) {
					ret = 1;
					break;
				}
			}
		}
		if (found == 0) {
			if ((ent = malloc(sizeof(struct regex_cache))) == NULL) {
				ret = 0;
				goto out;
			}
			if ((ent->pattern = strdup(words[i])) == NULL) {
				free(ent);
				ret = 0;
				goto out;
			}
			re = convert_re(words[i]);
			if (re == NULL) {
				free(ent->pattern);
				free(ent);
				ret = 0;
				goto out;
			}
			regcomp(&ent->reg, re, (ignorecase) ? REG_ICASE|REG_EXTENDED : REG_EXTENDED);
			free(re);
			tll_push_front(cache, ent);
			if (regexec(&ent->reg, origin, 0, NULL, 0) == 0) {
				ret = 1;
				break;
			}
		}
	}

out:
	free(words);
	free(buf);
	return (ret);
}

int
exec_updating(int argc, char **argv)
{
	char			*date = NULL;
	char			*dateline = NULL;
	char			*updatingfile = NULL;
	bool			caseinsensitive = false;
	struct installed_ports	*port;
	tll(struct installed_ports *) origins = tll_init();
	int			 ch;
	char			*line = NULL;
	size_t			 linecap = 0;
	char			*tmp;
	int			 head = 0;
	int			 found = 0;
	struct pkgdb		*db = NULL;
	struct pkg		*pkg = NULL;
	struct pkgdb_it		*it = NULL;
	FILE			*fd;
	int			 retcode = EXIT_SUCCESS;
#ifdef HAVE_CAPSICUM
	cap_rights_t rights;
#endif

	struct option longopts[] = {
		{ "date",	required_argument,	NULL,	'd' },
		{ "file",	required_argument,	NULL,	'f' },
		{ "case-insensitive",	no_argument,	NULL,	'i' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+d:f:i", longopts, NULL)) != -1) {
		switch (ch) {
		case 'd':
			date = optarg;
			break;
		case 'f':
			updatingfile = optarg;
			break;
		case 'i':
			caseinsensitive = true;
			break;
		default:
			usage_updating();
			return (EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	/* checking date format */
	if (date != NULL)
		if (strlen(date) != 8 || strspn(date, "0123456789") != 8)
			err(EXIT_FAILURE, "Invalid date format");

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EXIT_FAILURE);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EXIT_FAILURE);
	}

	if (updatingfile == NULL) {
		const char *portsdir = pkg_object_string(pkg_config_get("PORTSDIR"));
		if (portsdir == NULL) {
			retcode = EXIT_FAILURE;
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
		fclose(fd);
		return (EXIT_FAILURE);
	}

#ifndef PKG_COVERAGE
	if (cap_enter() < 0 && errno != ENOSYS) {
		warn("cap_enter() failed");
		fclose(fd);
		return (EXIT_FAILURE);
	}
#endif
#endif

	if (argc == 0) {
		if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL) {
			retcode = EXIT_FAILURE;
			fclose(fd);
			goto cleanup;
		}

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			port = malloc(sizeof(struct installed_ports));
			pkg_asprintf(&port->origin, "%o", pkg);
			tll_push_front(origins, port);
		}
	} else {
		while (*argv) {
			port = malloc(sizeof(struct installed_ports));
			port->origin = strdup(*argv);
			tll_push_front(origins, port);
			argv++;
		}
	}

	while (getline(&line, &linecap, fd) > 0) {
		if (strspn(line, "0123456789:") == 9) {
			free(dateline);
			dateline = strdup(line);
			found = 0;
			head = 1;
		} else if (head == 0) {
			continue;
		}

		tmp = NULL;
		if (found == 0) {
			if (strstr(line, "AFFECTS") != NULL) {
				tll_foreach(origins, it) {
					if (matcher(line, it->item->origin, caseinsensitive) != 0) {
						tmp = "";
						break;
					}
				}
				if (tmp == NULL)
					tmp = strcasestr(line, "all users\n");
				if (tmp == NULL)
					tmp = strcasestr(line, "all ports users\n");
				if (tmp != NULL) {
					if ((date != NULL) && strncmp(dateline, date, 8) < 0) {
						continue;
					}
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
	free(line);
	free(dateline);

	return (retcode);
}
