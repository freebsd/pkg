#include <sys/param.h>
#include <sys/queue.h>

#define _WITH_GETLINE
#include <err.h>
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
	fprintf(stderr, "usage: pkg updating [-h] [-d YYYYMMDD] [-f file] [portname ...]\n");

}

int
exec_updating(int argc, char **argv)
{
	char *date = NULL;
	char *dateline = NULL;
	char *updatingfile = NULL;
	struct installed_ports *port;
	SLIST_HEAD(,installed_ports) origins;
	int ch;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	char *tmp;
	int head = 0;
	int found = 0;
	struct pkgdb *db = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	const char *origin;
	FILE *fd;

	while ((ch = getopt(argc, argv, "d:f:")) != -1) {
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

	SLIST_INIT(&origins);
	if (argc == 0) {
		if (( it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL)
			goto cleanup;

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			port = malloc(sizeof(struct installed_ports));
			pkg_get(pkg, PKG_ORIGIN, &origin);
			port->origin = strdup(origin);
			SLIST_INSERT_HEAD(&origins, port, next);
		}
	} else {
	/* TODO missing per port */
	}

	if (updatingfile == NULL) {
		const char *portsdir;
		if (pkg_config_string(PKG_CONFIG_PORTSDIR, &portsdir) != EPKG_OK) {
			warnx("Cant get portsdir config entry");
			return (1);
		}
		asprintf(&updatingfile, "%s/UPDATING", portsdir);
	}

	fd = fopen(updatingfile, "r");
	if (fd == NULL)
		errx(EX_UNAVAILABLE, "Unable to open: %s", updatingfile);

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
	pkgdb_close(db);
	pkgdb_it_free(it);
	pkg_free(pkg);

	return (EXIT_SUCCESS);
}
