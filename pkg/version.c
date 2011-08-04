#include <unistd.h>
#include <stdlib.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <sysexits.h>
#include <sys/utsname.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <pkg.h>
#include <err.h>
#include "version.h"
#include <string.h>

static struct index_entry{
	char *origin;
	char *version;
	SLIST_ENTRY(index_entry) next;
};

void
usage_version(void)
{
	fprintf(stderr, "usage: pkg version [-hIoqv] [-l limchar] [-L limchar] [[-X] -s string]\n");
	fprintf(stderr, "                   [-O origin] [index]\n");
	fprintf(stderr, "       pkg version -t <version1> <version2>\n");
	fprintf(stderr, "       pkg version -T <pkgname> <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help version'.\n");
}

int
exec_version(int argc, char **argv)
{
	unsigned int opt = 0;
	int ch;
	FILE *indexfile;
	char indexpath[MAXPATHLEN];
	SLIST_HEAD(, index_entry) index;
	SLIST_INIT(&index);
	struct utsname u;
	int rel_major_ver;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	char *buf;
	char *version;
	struct index_entry *entry;
	struct pkgdb *db = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	char key;
	char limchar = '-';

	while ((ch = getopt(argc, argv, "hIoqvl:LXsOtT")) != -1) {
		switch (ch) {
			case 'h':
				usage_version();
				return (0);
			case 'I':
				opt |= VERSION_INDEX;
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
				break;
			case 'X':
				opt |= VERSION_EREGEX;
				break;
			case 's':
				opt |= VERSION_STRING;
				break;
			case 'O':
				opt |= VERSION_WITHORIGIN;
				break;
			case 't':
				opt |= VERSION_TESTVERSION;
				break;
			case 'T':
				opt |= VERSION_TESTPATTERN;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	/* -t must be unique */
	if (((opt & VERSION_TESTVERSION) && opt != VERSION_TESTVERSION) ||
			(opt == VERSION_TESTVERSION && argc < 2)) {
		usage_version();
		return (EX_USAGE);
	}

	else if (opt == VERSION_TESTVERSION) {
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
	} else if (opt & VERSION_INDEX) {
		uname(&u);
		rel_major_ver = (int) strtol(u.release, NULL, 10);
		snprintf(indexpath, MAXPATHLEN, "%s/INDEX-%d", pkg_config("PORTSDIR"), rel_major_ver);
		indexfile = fopen(indexpath, "r");
		if (!indexfile)
			err(EX_SOFTWARE, "Unable to open %s", indexpath);

		if (opt & VERSION_STATUS)
			if (limchar != '<' &&
					limchar != '>' &&
					limchar != '=') {
				usage_version();
				return (EX_USAGE);
			}

		while ((linelen = getline(&line, &linecap, indexfile)) > 0) {
			/* line is pkgname|portdir|... */
			buf = line;
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
			SLIST_INSERT_HEAD(&index, entry, next);
		}
		free(line);
		fclose(indexfile);

		if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
			return (EX_IOERR);

		if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL)
			goto cleanup;

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			key = '!';
			SLIST_FOREACH(entry, &index, next) {
				if (!strcmp(entry->origin, pkg_get(pkg, PKG_ORIGIN))) {
					switch (pkg_version_cmp(pkg_get(pkg, PKG_VERSION), entry->version)) {
						case -1:
							key = '<';
							break;
						case 0:
							key = '=';
							break;
						case 1:
							key = '>';
							break;
					}
					break;
				}
			}
			if (opt & VERSION_STATUS) {
				if ( limchar == key) {
					printf("%-34s %c\n", pkg_get(pkg, PKG_NAME), key);
				}
			} else {
				printf("%-34s %c\n", pkg_get(pkg, PKG_NAME), key);
			}
		}
	} else  {
		fprintf(stderr, "Not yet implemented please use -I \n");
		return (EX_SOFTWARE);
	}

cleanup:
	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (EPKG_OK);
}
