#include <sys/types.h>
#include <sys/param.h>

#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "install.h"

void
usage_install(void)
{
	fprintf(stderr, "usage: pkg install <pkg-name>\n");
	fprintf(stderr, "For more information see 'pkg help install'.\n");
}

int
exec_install(int argc, char **argv)
{
	char dbfile[MAXPATHLEN];

	int retcode = EPKG_OK;
	int i, multi_repos = 0;

	struct pkg_jobs *jobs = NULL;
	struct pkg_jobs_entry *je = NULL;

	struct pkg_repos *repos = NULL;
	struct pkg_repos_entry *re = NULL;

	struct pkg *pkg = NULL, *tmp = NULL;
	struct pkgdb *db = NULL;

	if (argc < 2) {
		usage_install();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("installing packages can only be done as root");
		return (EX_NOPERM);
	}

	/* create a jobs object */
	if (pkg_jobs_new(&jobs) != EPKG_OK) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	/*
	 * Honor PACKAGESITE if specified 
	 * Working on a single repo database
	 */
	if (pkg_config("PACKAGESITE") != NULL) {
                if (pkgdb_open(&db, PKGDB_REMOTE, "repo.sqlite") != EPKG_OK) {
                        warnx("cannot open repository database: %s/repo.sqlite\n", pkg_config("PKG_DBDIR"));
                        retcode = EPKG_FATAL;
                        goto cleanup;
                }

                /* create a jobs entry */
                if (pkg_jobs_new_entry(jobs, &je, PKG_JOBS_INSTALL, db) != EPKG_OK) {
                        retcode = EPKG_FATAL;
                        goto cleanup;
                }

		for (i = 1; i < argc; i++) {
			if ((pkg = pkgdb_query_remote(db, argv[i])) == NULL) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			
			/* pkg_jobs_resolv() will be enough here for jobs resolving :) */
			pkg_jobs_add(je, pkg);
		}
	} else {
		/* MULTI_REPOS_INSTALL */

		multi_repos = 1;

                fprintf(stderr, "\n");
                warnx("/!\\     Working on multiple repositories     /!\\");
                warnx("/!\\  This is an unsupported preview feature  /!\\");
                warnx("/!\\     It can kill kittens and puppies      /!\\");
                fprintf(stderr, "\n");

                if (pkg_repos_new(&repos) != EPKG_OK) {
                        retcode = EPKG_FATAL;
                        goto cleanup;
                }

                if (pkg_repos_load(repos) != EPKG_OK) {
                        retcode = EPKG_FATAL;
                        goto cleanup;
                }
        
                while (pkg_repos_next(repos, &re) == EPKG_OK) { 
                        snprintf(dbfile, MAXPATHLEN, "%s.sqlite", pkg_repos_get_name(re));

                        if (pkgdb_open(&db, PKGDB_REMOTE, dbfile) != EPKG_OK) {
                                warnx("cannot open repository database: %s/%s\n", 
                                                pkg_config("PKG_DBDIR"), dbfile);
                                retcode = EPKG_FATAL;
                                goto cleanup;
                        }

                        /* create a jobs entry for each db connection */
                        if (pkg_jobs_new_entry(jobs, &je, PKG_JOBS_INSTALL, db) != EPKG_OK) {
                                retcode = EPKG_FATAL;
                                goto cleanup;
                        }

        		for (i = 1; i < argc; i++) {
				if ((pkg = pkgdb_query_remote(db, argv[i])) == NULL) {
					retcode = EPKG_FATAL;
					goto cleanup;
				}
				
				/* 
				 * check if the job already exists in other job entries
				 * If it exists we add additional repo to the package.
				 * Otherwise we have a new package job.
				 */
				tmp = NULL;
				if (pkg_jobs_exists(jobs, pkg, &tmp) == EPKG_OK)
					pkg_jobs_add(je, pkg);
				else
					pkg_repos_add_in_pkg(tmp, re);
			}
                }
	} /* !MULTI_REPOS_INSTALL */

	/* print a summary before applying the jobs */
	printf("The following packages will be installed:\n");

	je = NULL; /* starts with the first job entry */
	while (pkg_jobs(jobs, &je) == EPKG_OK) {
		pkg = NULL; /* start with the first package in a job entry */
		while (pkg_jobs_entry(je, &pkg) == EPKG_OK) {
			printf("\t%s-%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

			if (multi_repos == 1) {
				printf(" [ found in repos: ");

				re = NULL;
				while (pkg_repos_next_in_pkg(pkg, &re) == EPKG_OK)
					printf("%s ", pkg_repos_get_name(re));

				printf(" ]");
			}

			printf("\n");
		}
	}

	je = NULL;
	while (pkg_jobs(jobs, &je) == EPKG_OK)
		retcode = pkg_jobs_apply(je, 0);

	cleanup:
	
	if (multi_repos == 1)
		pkg_repos_free(repos);

	/* db connections are closed by pkg_jobs_free() */
	pkg_jobs_free(jobs);

	return (retcode == EPKG_OK ? EX_OK : 1);
}
