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

static int update_from_remote_repo(const char *name, const char *url);

static int
update_from_remote_repo(const char *name, const char *url)
{
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;
	char repofile[MAXPATHLEN];
	char repofile_unchecked[MAXPATHLEN];
	char tmp[21];
	const char *dbdir = NULL;
	unsigned char *sig = NULL;
	int siglen = 0;
	int rc = EPKG_OK;

	(void)strlcpy(tmp, "/tmp/repo.txz.XXXXXX", sizeof(tmp));
	if (mktemp(tmp) == NULL) {
		warnx("Could not create temporary file %s, aborting update.\n", tmp);
		return (EPKG_FATAL);
	}

	if (pkg_config_string(PKG_CONFIG_DBDIR, &dbdir) != EPKG_OK) {
		warnx("Cant get dbdir config entry");
		return (EPKG_FATAL);
	}

	if (pkg_fetch_file(url, tmp) != EPKG_OK) {
		/*
		 * No need to unlink(tmp) here as it is already
		 * done in pkg_fetch_file() in case fetch failed.
		 */
		return (EPKG_FATAL);
	}

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	archive_read_open_filename(a, tmp, 4096);

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), "repo.sqlite") == 0) {
			snprintf(repofile, sizeof(repofile), "%s/%s.sqlite", dbdir, name);
			snprintf(repofile_unchecked, sizeof(repofile_unchecked), "%s.unchecked", repofile);
			archive_entry_set_pathname(ae, repofile_unchecked);
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
		}
		if (strcmp(archive_entry_pathname(ae), "signature") == 0) {
			siglen = archive_entry_size(ae);
			sig = malloc(siglen);
			archive_read_data(a, sig, siglen);
		}
	}

	if (sig != NULL) {
		if (pkg_repo_verify(repofile_unchecked, sig, siglen - 1) != EPKG_OK) {
			warnx("Invalid signature, removing repository.\n");
			unlink(repofile_unchecked);
			free(sig);
			rc = EPKG_FATAL;
			goto cleanup;
		}
	}

	rename(repofile_unchecked, repofile);

cleanup:
	if (a != NULL)
		archive_read_finish(a);

	(void)unlink(tmp);

	return (rc);
}

void
usage_update(void)
{
	fprintf(stderr, "usage: pkg update\n\n");
	fprintf(stderr, "For more information see 'pkg help update'.\n");
}

int
exec_update(int argc, char **argv)
{
	char url[MAXPATHLEN];
	const char *packagesite = NULL;
	const char *repo_name;
	struct pkg_config_kv *repokv = NULL;
	int retcode = EPKG_OK;
	bool multi_repos = false;

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
	 * Fetch remote databases.
	 */

	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multi_repos);

	/* single repository */
	if (!multi_repos) {
		/*
		 * Single remote database
		 */
		
		pkg_config_string(PKG_CONFIG_REPO, &packagesite);
		
		if (packagesite == NULL) {
			warnx("PACKAGESITE is not defined.");
			return (1);
		}

		if (packagesite[strlen(packagesite) - 1] == '/')
			snprintf(url, MAXPATHLEN, "%srepo.txz", packagesite);
		else
			snprintf(url, MAXPATHLEN, "%s/repo.txz", packagesite);

		retcode = update_from_remote_repo("repo", url);
	} else {
		/* multiple repositories */
		while (pkg_config_list(PKG_CONFIG_REPOS, &repokv) == EPKG_OK) {
			repo_name = pkg_config_kv_get(repokv, PKG_CONFIG_KV_KEY);
			packagesite = pkg_config_kv_get(repokv, PKG_CONFIG_KV_VALUE);

			if (packagesite[strlen(packagesite) - 1] == '/')
				snprintf(url, MAXPATHLEN, "%srepo.txz", packagesite);
			else
				snprintf(url, MAXPATHLEN, "%s/repo.txz", packagesite);

			retcode = update_from_remote_repo(repo_name, url);
		}
	}

	return (retcode);
}
