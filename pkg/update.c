#include <sys/param.h>
#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

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
	fprintf(stderr, "usage: pkg update\n\n");
	fprintf(stderr, "For more information see 'pkg help update'.\n");
}

int
exec_update(int argc, char **argv)
{
	char url[MAXPATHLEN + 1];
	const char *packagesite = NULL;
	char *tmp = NULL;
	int retcode = EPKG_OK;
	struct archive *a;
	struct archive_entry *ae;
	unsigned char *sig = NULL;
	int siglen = 0;

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

	if (packagesite[strlen(packagesite) - 1] == '/')
		snprintf(url, sizeof(url), "%srepo.txz", packagesite);
	else
		snprintf(url, sizeof(url), "%s/repo.txz", packagesite);

	tmp = mktemp(strdup("/tmp/repo.txz.XXXXXX"));

	if (pkg_fetch_file(url, tmp) != EPKG_OK) {
		unlink(tmp);
		free(tmp);
		return (EPKG_FATAL);
	}

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	archive_read_open_filename(a, tmp, 4096);

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), "repo.sqlite") == 0) {
			archive_entry_set_pathname(ae, "/var/db/pkg/repo.sqlite.unchecked");
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
		}
		if (strcmp(archive_entry_pathname(ae), "signature") == 0) {
			siglen = archive_entry_size(ae);
			sig = malloc(siglen);
			archive_read_data(a, sig, siglen);
		}
	}

	if (sig != NULL) {
		if (pkg_repo_verify( "/var/db/pkg/repo.sqlite.unchecked", sig, siglen - 1) != EPKG_OK) {
			fprintf(stderr, "Invalid signature removing\n");
			unlink("/var/db/pkg/repo.sqlite.unchecked");
			free(sig);
			return (EPKG_FATAL);
		}
	}
	rename("/var/db/pkg/repo.sqlite.unchecked", "/var/db/pkg/repo.sqlite");

	if ( a != NULL)
		archive_read_finish(a);

	unlink(tmp);
	free(tmp);

	return (retcode);
}
