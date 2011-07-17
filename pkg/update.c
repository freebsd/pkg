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
	const char *packagesite = NULL;
	char *tmp = NULL;
	int retcode = 0;
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

	if ((packagesite = pkg_config("PACKAGESITE")) == NULL) {
		warnx("unable to determine PACKAGESITE");
		return (1);
	}

	if (packagesite[strlen(packagesite) - 1] == '/')
		snprintf(url, MAXPATHLEN, "%srepo.txz", packagesite);
	else
		snprintf(url, MAXPATHLEN, "%s/repo.txz", packagesite);

	tmp = mktemp(strdup("/tmp/repo.txz.XXXXXX"));

	if (pkg_fetch_file(url, tmp) != EPKG_OK) {
		retcode = 1;
		goto cleanup;
	}

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	archive_read_open_filename(a, tmp, 4096);

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), "repo.sqlite") == 0) {
			archive_entry_set_pathname(ae, "/var/db/pkg/repo.sqlite");
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
