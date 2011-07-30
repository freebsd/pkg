#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

int
pkg_repo_fetch(struct pkg *pkg)
{
	char dest[MAXPATHLEN];
	char cksum[65];
	char *path;
	char *url;
	int retcode = EPKG_OK;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE ||
		(pkg->type & PKG_UPGRADE) == PKG_UPGRADE);

	snprintf(dest, sizeof(dest), "%s/%s", pkg_config("PKG_CACHEDIR"),
			 pkg_get(pkg, PKG_REPOPATH));

	/* If it is already in the local cachedir, dont bother to download it */
	if (access(dest, F_OK) == 0)
		goto checksum;

	/* Create the dirs in cachedir */
	if ((path = dirname(dest)) == NULL) {
		EMIT_ERRNO("dirname", dest);
		retcode = EPKG_FATAL;
		goto cleanup;
	}
	if ((retcode = mkdirs(path)) != 0)
		goto cleanup;

	asprintf(&url, "%s/%s", pkg_config("PACKAGESITE"),
			 pkg_get(pkg, PKG_REPOPATH));

	retcode = pkg_fetch_file(url, dest);
	free(url);
	if (retcode != EPKG_OK)
		goto cleanup;

	checksum:
	retcode = sha256_file(dest, cksum);
	if (retcode == EPKG_OK)
		if (strcmp(cksum, pkg_get(pkg, PKG_CKSUM))) {
			EMIT_FAILED_CKSUM(pkg);
			retcode = EPKG_FATAL;
		}

	cleanup:
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}
