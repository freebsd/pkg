#include <sys/param.h>
#include <sys/types.h>

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <libutil.h>

#include <archive.h>
#include <archive_entry.h>

#include <pkg.h>

#include "update.h"

#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM| \
		ARCHIVE_EXTRACT_TIME  |ARCHIVE_EXTRACT_ACL | \
		ARCHIVE_EXTRACT_FFLAGS|ARCHIVE_EXTRACT_XATTR)

static int update_from_remote_repo(const char *name, const char *url);

void
usage_update(void)
{
	fprintf(stderr, "usage: pkg update\n\n");
	fprintf(stderr, "For more information see 'pkg help update'.\n");
}

int
exec_update(int argc, char **argv)
{
	struct pkg_remote_repo *repo;
	char url[MAXPATHLEN];
	const char *packagesite = NULL;
	int retcode = EPKG_OK;

	(void)argv;
	if (argc != 1) {
		usage_update();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("updating the remote database can only be done as root");
		return (EX_NOPERM);
	}

	/* 
	 * If PACKAGESITE is defined fetch only the remote
	 * database to which PACKAGESITE refers, otherwise
	 * fetch all remote databases found in the configuration 
	 * file.
	 */
	if ((packagesite = pkg_config("PACKAGESITE")) != NULL) {
		if (packagesite[strlen(packagesite) - 1] == '/')
			snprintf(url, MAXPATHLEN, "%srepo.txz", packagesite);
		else
			snprintf(url, MAXPATHLEN, "%s/repo.txz", packagesite);

		retcode = update_from_remote_repo("repo", url);
	} else {
		pkg_remote_repo_init();
		pkg_remote_repo_load();

		while ((repo = pkg_remote_repo_next()) != NULL) {
			if (repo->url[strlen(repo->url) - 1] == '/')
				snprintf(url, MAXPATHLEN, "%srepo.txz", repo->url);
			else
				snprintf(url, MAXPATHLEN, "%s/repo.txz", repo->url);

			retcode = update_from_remote_repo(repo->name, url);
		}

		pkg_remote_repo_free();
	}

	return (retcode);
}

int
update_from_remote_repo(const char *name, const char *url)
{
	struct archive *a = NULL;
	struct archive_entry *ae;
	char repofile[MAXPATHLEN];
	char *tmp = NULL;
	int retcode = EPKG_OK;

	tmp = mktemp(strdup("/tmp/repo.txz.XXXXXX"));

	if (pkg_fetch_file(url, tmp) != EPKG_OK) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	archive_read_open_filename(a, tmp, 4096);

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), "repo.sqlite") == 0) {
			snprintf(repofile, MAXPATHLEN, "%s/%s.sqlite",
				       pkg_config("PKG_DBDIR"), name);
			archive_entry_set_pathname(ae, repofile);
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
			break;
		}
	}

	cleanup:

	if ( a != NULL) 
		archive_read_finish(a);

	unlink(tmp);
	free(tmp);

	return(retcode);
}
