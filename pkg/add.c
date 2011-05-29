#include <sys/types.h>

#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "add.h"

static void
fetch_status(__unused void *data, const char *url, off_t total, off_t done,
			 __unused time_t elapsed)
{
	unsigned int percent;

	percent = ((float)done / (float)total) * 100;
	printf("\rFetching %s... %d%%", url, percent);

	if (done == total)
		printf("\n");

	fflush(stdout);
}

static int
is_url(const char *pattern)
{
	if (strncmp(pattern, "http://", 7) == 0 ||
		strncmp(pattern, "https://", 8) == 0 ||
		strncmp(pattern, "ftp://", 6) == 0)
		return (EPKG_OK);

	return (EPKG_FATAL);
}

static void
install_status(__unused void *data, struct pkg *pkg)
{
	printf("Installing %s-%s...\n", pkg_get(pkg, PKG_NAME),
		   pkg_get(pkg, PKG_VERSION));
}

static int
add_from_repo(const char *name)
{
	struct pkg *pkg = NULL;
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	int retcode = EPKG_OK;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		pkg_error_warn("can not open database");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (pkg_jobs_new(&jobs, db) != EPKG_OK) {
		pkg_error_warn("pkg_jobs_new()");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if ((pkg = pkgdb_query_remote(db, name)) == NULL) {
		retcode = pkg_error_number();
		pkg_error_warn("can query the database");
		goto cleanup;
	}

	pkg_jobs_add(jobs, pkg);

	/* print a summary before applying the jobs */
	pkg = NULL;
	printf("The following packages will be installed:\n");
	while (pkg_jobs(jobs, &pkg) == EPKG_OK) {
		printf("%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	}

	if (pkg_jobs_apply(jobs, NULL, fetch_status, install_status) != EPKG_OK)
		pkg_error_warn("can not install");

	cleanup:
	if (db != NULL)
		pkgdb_close(db);
	if (jobs != NULL)
		pkg_jobs_free(jobs);
	return (EPKG_OK);
}

static int
add_from_file(const char *file)
{
	struct pkgdb *db = NULL;
	struct pkg *pkg = NULL;
	const char *message;
	int retcode = EPKG_OK;

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkg_error_warn("can not open database");
		retcode = EPKG_FATAL;
	}

	if (pkg_add(db, file, &pkg) != EPKG_OK) {
		pkg_error_warn("can not install %s", file);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	message = pkg_get(pkg, PKG_MESSAGE);
	if (message != NULL && message[0] != '\0')
		printf("%s", message);

	cleanup:
	if (db != NULL)
		pkgdb_close(db);
	if (pkg != NULL)
		pkg_free(pkg);

	return (retcode);
}

void
usage_add(void)
{
	fprintf(stderr, "usage: pkg add <pkg-name>\n");
	fprintf(stderr, "       pkg add <url>://<pkg-name>\n\n");
	fprintf(stderr, "For more information see 'pkg help add'.\n");
}

int
exec_add(int argc, char **argv)
{
	char *name;
	int retcode = 0;

	if (argc != 2) {
		usage_add();
		return (-1);
	}

	if (geteuid() != 0) {
		warnx("adding packages can only be done as root");
		return (EX_NOPERM);
	}

	if (is_url(argv[1]) == EPKG_OK) {
		asprintf(&name, "./%s", basename(argv[1]));
		if (pkg_fetch_file(argv[1], name, NULL, &fetch_status) != EPKG_OK) {
			pkg_error_warn("can not fetch %s", argv[1]);
			return (1);
		}
	} else
		name = argv[1];

	/* if it is a file  */
	if (name[0] == '/' || name[0] == '.') {
		if (access(name, F_OK) == 0) {
			retcode = add_from_file(name);
		} else {
			warn("%s", name);
		}
	} else {
		retcode = add_from_repo(name);
	}

	return (retcode == EPKG_OK ? 0 : 1);
}

