#include <sys/param.h>
#include <sys/queue.h>

#include <err.h>
#include <assert.h>
#include <sysexits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

struct deps_entry {
	char *name;
	char *version;
	char *origin;
	STAILQ_ENTRY(deps_entry) next;
};

STAILQ_HEAD(deps_head, deps_entry);

static int check_deps(struct pkgdb *db, struct pkg *pkg, struct deps_head *dh);
static void add_missing_dep(struct pkg_dep *d, struct deps_head *dh);
static void deps_free(struct deps_head *dh);
static int fix_deps(struct pkgdb *db, struct deps_head *dh, int nbpkgs, bool yes);
static void check_summary(struct pkgdb *db, struct deps_head *dh);

static int
check_deps(struct pkgdb *db, struct pkg *p, struct deps_head *dh)
{
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	struct pkgdb_it *it = NULL;
	char *name, *version, *origin;
	int nbpkgs = 0;

	assert(db != NULL);
	assert(p != NULL);

	name = version = origin = NULL;
	pkg_get(p, PKG_NAME, &name, PKG_VERSION, &version, PKG_ORIGIN, &origin);

	while (pkg_deps(p, &dep) == EPKG_OK) {
		if ((it = pkgdb_query(db, pkg_dep_get(dep, PKG_DEP_ORIGIN), MATCH_EXACT)) == NULL)
			return (0);;
		
		/* do we have a missing dependency? */
		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
			printf("%s has a missing dependency: %s\n", origin,
					pkg_dep_get(dep, PKG_DEP_ORIGIN)),
			add_missing_dep(dep, dh);
			nbpkgs++;
		}

		pkgdb_it_free(it);
	}
	
	pkg_free(pkg);

	return (nbpkgs);
}

static void
add_missing_dep(struct pkg_dep *d, struct deps_head *dh)
{
	struct deps_entry *e = NULL;
	const char *origin = NULL;

	assert(d != NULL);

	/* do not add duplicate entries in the queue */
	STAILQ_FOREACH(e, dh, next) {
		origin = pkg_dep_get(d, PKG_DEP_ORIGIN);
		if (strcmp(e->origin, origin) == 0)
			return;
	}

	if ((e = calloc(1, sizeof(struct deps_entry))) == NULL)
		err(1, "calloc(deps_entry)");

	e->name = strdup(pkg_dep_get(d, PKG_DEP_NAME));
	e->version = strdup(pkg_dep_get(d, PKG_DEP_VERSION));
	e->origin = strdup(pkg_dep_get(d, PKG_DEP_ORIGIN));

	STAILQ_INSERT_TAIL(dh, e, next);
}

static void
deps_free(struct deps_head *dh)
{
	struct deps_entry *e = NULL;

	while (!STAILQ_EMPTY(dh)) {
		e = STAILQ_FIRST(dh);
		STAILQ_REMOVE_HEAD(dh, next);
		free(e->name);
		free(e->version);
		free(e->origin);
		free(e);
	}
}

static int
fix_deps(struct pkgdb *db, struct deps_head *dh, int nbpkgs, bool yes)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg_jobs *jobs = NULL;
	struct deps_entry *e = NULL;
	char **pkgs = NULL;
	int64_t dlsize = 0;
	int64_t oldsize = 0, newsize = 0;
	char size[7];
	int i = 0;

	assert(db != NULL);
	assert(nbpkgs > 0);

	if ((pkgs = calloc(nbpkgs, MAXPATHLEN + 1)) == NULL)
		err(1, "calloc()");

	STAILQ_FOREACH(e, dh, next)
		pkgs[i++] = e->origin;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		return (EPKG_FATAL);;

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db) != EPKG_OK)
		free(pkgs);


        if ((it = pkgdb_query_installs(db, MATCH_EXACT, nbpkgs, pkgs, NULL)) == NULL) {
		free(pkgs);
		pkg_jobs_free(jobs);
	}

        while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) == EPKG_OK) {
                pkg_jobs_add(jobs, pkg);
                pkg = NULL;
        }

        if (pkg_jobs_is_empty(jobs)) {
                printf("\n>>> Not able to find packages for installation.\n\n");
		return (EPKG_FATAL);
        }

        /* print a summary before applying the jobs */
        pkg = NULL;
        printf("The following packages will be installed:\n");

        while (pkg_jobs(jobs, &pkg) == EPKG_OK) {
                const char *name, *version, *newversion;
                int64_t flatsize, newflatsize, pkgsize;
                pkg_get(pkg, PKG_NEWVERSION, &newversion, PKG_NAME, &name,
                    PKG_VERSION, &version, PKG_FLATSIZE, &flatsize,
                    PKG_NEW_FLATSIZE, &newflatsize, PKG_NEW_PKGSIZE, &pkgsize);
                dlsize += pkgsize;
                if (newversion != NULL) {
                        printf("\tUpgrading %s: %s -> %s\n", name, version, newversion);
                        oldsize += flatsize;
                        newsize += flatsize;
                } else {
                        newsize += flatsize;
                        printf("\tInstalling %s: %s\n", name, version);
                }
        }

        if (oldsize > newsize) {
                newsize *= -1;
                humanize_number(size, sizeof(size), oldsize - newsize, "B", HN_AUTOSCALE, 0);
                printf("\nthe installation will save %s\n", size);
        } else {
                humanize_number(size, sizeof(size), newsize - oldsize, "B", HN_AUTOSCALE, 0);
                printf("\nThe installation will require %s more space\n", size);
        }
        humanize_number(size, sizeof(size), dlsize, "B", HN_AUTOSCALE, 0);
        printf("%s to be downloaded\n", size);

	if (yes == false)
		yes = query_yesno("\n>>> Try to fix the missing dependencies [y/N]: ");

	if (yes == true)
		pkg_jobs_apply(jobs, 0);

	free(pkgs);
	pkg_free(pkg);
	pkg_jobs_free(jobs);
	pkgdb_it_free(it);

	return (EPKG_OK);
}

static void
check_summary(struct pkgdb *db, struct deps_head *dh)
{
	struct deps_entry *e = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	bool fixed = true;

	assert(db != NULL);

	printf(">>> Summary of actions performed:\n\n");
		
	STAILQ_FOREACH(e, dh, next) {
		if ((it = pkgdb_query(db, e->origin, MATCH_EXACT)) == NULL)
			return;
		
		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
			fixed = false;
			printf("%s dependency failed to be fixed\n", e->origin);
		} else
			printf("%s dependency has been fixed\n", e->origin);

		pkgdb_it_free(it);
	}
	
	if (fixed) {
		printf("\n>>> Missing dependencies were fixed successfully.\n");
	} else {
		printf("\n>>> There are still missing dependencies.\n");
		printf(">>> You are advised to try fixing them manually.\n");
		printf("\n>>> Also make sure to check 'pkg updating' for known issues.\n");
	}

	pkg_free(pkg);
}

void
usage_check(void)
{
	fprintf(stderr, "usage: pkg check [-y]\n\n");
	fprintf(stderr, "For more information see 'pkg help check'.\n");
}

int
exec_check(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	struct pkgdb *db = NULL;
	int retcode = EX_OK;
	int ret;
	int ch;
	bool yes = false;
	int nbpkgs = 0;

	struct deps_head dh = STAILQ_HEAD_INITIALIZER(dh);

	while ((ch = getopt(argc, argv, "y")) != -1) {
		switch (ch) {
			case 'y':
				yes = true;
				break;
			default:
				usage_check();
				return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0) {
		usage_check();
		return (EX_USAGE);
	}

	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret == EPKG_ENODB) {
		if (geteuid() == 0)
			return (EX_IOERR);

		return (retcode);
	}

	if (ret != EPKG_OK)
		return (EX_IOERR);

	if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	/* check for missing dependencies */
	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) == EPKG_OK) 
		nbpkgs += check_deps(db, pkg, &dh);

	if (geteuid() == 0 && nbpkgs > 0) {
		if (yes == false) 
			pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);

		printf("\n>>> Missing package dependencies were detected.\n\n");
		ret = fix_deps(db, &dh, nbpkgs, yes);
		if (ret == EPKG_OK)
			check_summary(db, &dh);
	}

	deps_free(&dh);
	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (retcode);
}
