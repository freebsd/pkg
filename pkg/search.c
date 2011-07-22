#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <libutil.h>
#include <sysexits.h>

#include <pkg.h>

#include "search.h"

static int search_remote_repo(const char *pattern, match_t match, 
		unsigned int field, const char *dbname);

void
usage_search(void)
{
	fprintf(stderr, "usage: pkg search [-gxXcd] pattern\n\n");
	fprintf(stderr, "For more information see 'pkg help search'.\n");
}

int
exec_search(int argc, char **argv)
{
	char *pattern = NULL;
	match_t match = MATCH_EXACT;
	unsigned int field = REPO_SEARCH_NAME;
	int retcode = EPKG_OK;
	int ch;
	struct pkg_repos *repos = NULL;
	struct pkg_repos_entry *re = NULL;

	while ((ch = getopt(argc, argv, "gxXcd")) != -1) {
		switch (ch) {
			case 'g':
				match = MATCH_GLOB;
				break;
			case 'x':
				match = MATCH_REGEX;
				break;
			case 'X':
				match = MATCH_EREGEX;
				break;
			case 'c':
				field |= REPO_SEARCH_COMMENT;
				break;
			case 'd':
				field |= REPO_SEARCH_DESCRIPTION;
				break;
			default:
				usage_search();
				return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage_search();
		return (EX_USAGE);
	}

	pattern = argv[0];

	/*
	 * TODO: Implement a feature to search only
	 * in a given repository specified in the argument list
	 */

	/* 
	 * Honor PACKAGESITE if specified
	 */
	if (pkg_config("PACKAGESITE") != NULL) {
		retcode = search_remote_repo(pattern, match, field, "repo");
	} else {
		fprintf(stderr, "\n");
		warnx("/!\\     Working on multiple repositories     /!\\");
		warnx("/!\\  This is an unsupported preview feature  /!\\");
		warnx("/!\\     It can kill kittens and puppies      /!\\");
		fprintf(stderr, "\n");

		if (pkg_repos_new(&repos) != EPKG_OK)
			return (EPKG_FATAL);

		if (pkg_repos_load(repos) != EPKG_OK)
			return (EPKG_FATAL);
	
		while (pkg_repos_next(repos, &re) == EPKG_OK)
			retcode = search_remote_repo(pattern, match, field, pkg_repos_get_name(re));

		pkg_repos_free(repos);
	}

	return (retcode);
}

static int
search_remote_repo(const char *pattern, match_t match, unsigned int field, const char *dbname)
{
	char size[7];
	char dbfile[MAXPATHLEN];
	int  retcode = EPKG_OK;
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;

	snprintf(dbfile, MAXPATHLEN, "%s.sqlite", dbname);

	if (pkgdb_open(&db, PKGDB_REMOTE, dbfile) != EPKG_OK) {
		warnx("cannot open repository database: %s/%s\n", 
				pkg_config("PKG_DBDIR"), dbfile);
		return (EPKG_FATAL);
	}

	if ((it = pkgdb_rquery(db, pattern, match, field)) == NULL) {
		warnx("cannot query repository database: %s/%s\n",
				pkg_config("PKG_DBDIR"), dbfile);
		pkgdb_it_free(it);
		pkgdb_close(db);
		return (EPKG_FATAL);
	}

	while ((retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		printf("Name:       %s\n", pkg_get(pkg, PKG_NAME));
		printf("Version:    %s\n", pkg_get(pkg, PKG_VERSION));
		printf("Origin:     %s\n", pkg_get(pkg, PKG_ORIGIN));
		printf("Arch:       %s\n", pkg_get(pkg, PKG_ARCH));
		printf("Maintainer: %s\n", pkg_get(pkg, PKG_MAINTAINER));
		printf("WWW:        %s\n", pkg_get(pkg, PKG_WWW));
		printf("Comment:    %s\n", pkg_get(pkg, PKG_COMMENT));
		printf("Repository: %s\n", dbname);
		humanize_number(size, sizeof(size), pkg_new_flatsize(pkg), "B", HN_AUTOSCALE, 0);
		printf("Flat size:  %s\n", size);
		humanize_number(size, sizeof(size), pkg_new_pkgsize(pkg), "B", HN_AUTOSCALE, 0);
		printf("Pkg size:   %s\n", size);
		printf("\n");
	}

	return (retcode);
}
