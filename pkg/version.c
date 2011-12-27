#include <sys/param.h>
#include <sys/queue.h>
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

#include "version.h"

struct index_entry {
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

static struct sbuf *
exec_buf(const char *cmd) {
	FILE *fp;
	char buf[BUFSIZ];
	struct sbuf *res;

	if ((fp = popen(cmd, "r")) == NULL)
		return (NULL);

	res = sbuf_new_auto();
	while (fgets(buf, BUFSIZ, fp) != NULL)
		sbuf_cat(res, buf);

	pclose(fp);

	if (sbuf_len(res) == 0) {
		sbuf_delete(res);
		return (NULL);
	}

	sbuf_finish(res);

	return (res);
}

static void
print_version(struct pkg *pkg, const char *source, const char *ver, char limchar, unsigned int opt)
{
	bool to_print = true;
	char key;
	const char *version, *name, *origin;

	pkg_get(pkg, PKG_VERSION, &version, PKG_NAME, &name, PKG_ORIGIN, &origin);
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

	if (to_print == false)
		return;

	printf("%-34s %c", name, key);

	if (opt & VERSION_VERBOSE) {
		switch(key) {
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
			printf("   orphaned: %s", origin);
		case '!':
			printf("   Comparison failed");
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
	char indexpath[MAXPATHLEN + 1];
	SLIST_HEAD(, index_entry) indexhead;
	struct utsname u;
	int rel_major_ver;
	int retval;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	char *buf;
	char *version;
	struct index_entry *entry;
	struct pkgdb *db = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	char limchar = '-';
	struct sbuf *cmd;
	struct sbuf *res;
	const char *portsdir;
	const char *origin;

	SLIST_INIT(&indexhead);

	while ((ch = getopt(argc, argv, "hIoqvl:L:XsOtT")) != -1) {
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
				limchar = *optarg;
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

	if (pkg_config_string(PKG_CONFIG_PORTSDIR, &portsdir) != EPKG_OK)
		err(1, "Can not get portsdir config entry");

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
	} else if (opt & VERSION_INDEX) {
		uname(&u);
		rel_major_ver = (int) strtol(u.release, NULL, 10);
		snprintf(indexpath, sizeof(indexpath), "%s/INDEX-%d", portsdir, rel_major_ver);
		indexfile = fopen(indexpath, "r");
		if (!indexfile)
			err(EX_SOFTWARE, "Unable to open %s", indexpath);

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
			SLIST_INSERT_HEAD(&indexhead, entry, next);
		}
		free(line);
		fclose(indexfile);

		if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
			return (EX_IOERR);

		if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL)
			goto cleanup;

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			SLIST_FOREACH(entry, &indexhead, next) {
				pkg_get(pkg, PKG_ORIGIN, &origin);
				if (!strcmp(entry->origin, origin)) {
					print_version(pkg, "index", entry->version, limchar, opt);
					break;
				}
			}
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
		} else return fnmatch(argv[1], argv[0], 0);
		
		retval = FNM_NOMATCH;
		
		while ((linelen = getline(&line, &linecap, stdin)) > 0) {
			line[linelen - 1] = '\0'; /* Strip trailing newline */
			if ((ch == 0 && (fnmatch(argv[1], line, 0) == 0))
				|| (ch == 1 && (fnmatch(line, argv[0], 0) == 0))) {
				retval = EPKG_OK;
				printf("%.*s\n", (int)linelen, line);
			}
		}
		
		return (retval);
		
	} else  {
		if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
			return (EX_IOERR);

		if (( it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL)
			goto cleanup;

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			cmd = sbuf_new_auto();
			pkg_get(pkg, PKG_ORIGIN, &origin);
			sbuf_printf(cmd, "make -C %s/%s -VPKGVERSION", portsdir, origin);
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
				print_version(pkg, NULL, NULL, limchar, opt);
			}
			sbuf_delete(cmd);
		}
	}
	
cleanup:
	while (!SLIST_EMPTY(&indexhead)) {
		entry = SLIST_FIRST(&indexhead);
		SLIST_REMOVE_HEAD(&indexhead, next);
		free(entry->version);
		free(entry->origin);
		free(entry);
	}

	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (EPKG_OK);
}
