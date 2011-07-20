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

void
usage_update(void)
{
	fprintf(stderr, "usage pkg update\n\n");
	fprintf(stderr, "For more information see 'pkg help update'.\n");
}

int
exec_update(int argc, char **argv)
{
	char url[MAXPATHLEN];
	char repo_db[MAXPATHLEN];
	const char *packagesite = NULL, *repo_path = NULL;
	const char *repo_archive = NULL, *repo_file = NULL;
	char *tmp = NULL;
	int retcode = EPKG_OK;
	struct archive *a;
	struct archive_entry *ae;

	(void)argv;
	if (argc != 1) {
		usage_update();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("updating the remote database can only be done as root");
		return (EX_NOPERM);
	}

	if ((packagesite = pkg_config("PACKAGESITE")) == NULL) {
		warnx("unable to determine PACKAGESITE");
		return (EPKG_FATAL);
	}

	repo_archive = pkg_config("PKG_REPO_ARCHIVE");

	if (packagesite[strlen(packagesite) - 1] == '/')
		snprintf(url, MAXPATHLEN, "%s%s", packagesite, repo_archive);
	else
		snprintf(url, MAXPATHLEN, "%s/%s", packagesite, repo_archive);

	tmp = mktemp(strdup("/tmp/repo.txz.XXXXXX"));

	if (pkg_fetch_file(url, tmp) != EPKG_OK) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	archive_read_open_filename(a, tmp, 4096);

	repo_path = pkg_config("PKG_DBDIR");
	repo_file = pkg_config("PKG_DBFILE_REMOTE");
	snprintf(repo_db, MAXPATHLEN, "%s/%s", repo_path, repo_file);

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), repo_file) == 0) {
			archive_entry_set_pathname(ae, repo_db);
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
			break;
		}
	}

	cleanup:
	archive_read_finish(a);
	unlink(tmp);
	free(tmp);

	return (retcode);
}
