#include <sys/param.h>
#include <sys/types.h>

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <archive.h>
#include <archive_entry.h>

#include <pkg.h>

#include "update.h"

#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM| \
		ARCHIVE_EXTRACT_TIME  |ARCHIVE_EXTRACT_ACL | \
		ARCHIVE_EXTRACT_FFLAGS|ARCHIVE_EXTRACT_XATTR)

static void
fetch_status(void *data, const char *url, off_t total, off_t done, time_t elapsed)
{
	unsigned int percent;

	elapsed = 0;

	int *size = (int *)data;
	*size = total;
	percent = ((float)done / (float)total) * 100;
	printf("\rFetching %s... %d%%", url, percent);

	if (done == total)
		printf("\n");

	fflush(stdout);
}

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
	char *packagesite = NULL;
	int retcode = 0;
	int size = 0;
	char *repo;
	struct archive *a;
	struct archive_entry *ae;

	(void)argv;
	if (argc != 1) {
		usage_update();
		return (-1);
	}

	if (geteuid() != 0) {
		warnx("updating the remote database can only be done as root");
		return (EX_NOPERM);
	}

	packagesite = getenv("PACKAGESITE");

	if (packagesite == NULL)
		return (-2);

	if (packagesite[strlen(packagesite) - 1] == '/')
		snprintf(url, MAXPATHLEN, "%srepo.txz", packagesite);
	else
		snprintf(url, MAXPATHLEN, "%s/repo.txz", packagesite);

	if (pkg_fetch_buffer(url, &repo, &size, &fetch_status) != EPKG_OK) {
		pkg_error_warn("can not fetch %s", url);
		retcode = 1;
	}

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	archive_read_open_memory(a, repo, size);

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), "repo.sqlite") == 0) {
			archive_entry_set_pathname(ae, "/var/db/pkg/repo.sqlite");
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
			break;
		}
	}

	archive_read_finish(a);
	return (retcode);
}
