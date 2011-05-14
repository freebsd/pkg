#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"

int
pkg_repo_fetch(struct pkg *pkg, void *data, fetch_cb cb)
{
	char dest[MAXPATHLEN];
	char cksum[65];
	char *url;
	int retcode = EPKG_OK;

	if ((pkg->type & PKG_REMOTE) != PKG_REMOTE)
		return (ERROR_BAD_ARG("pkg"));

	snprintf(dest, sizeof(dest), "%s/%s", pkg_config("PKG_CACHEDIR"),
			 pkg_get(pkg, PKG_REPOPATH));

	/* If it is already in the local cachedir, dont bother to download it */
	if (access(dest, F_OK) == 0)
		goto checksum;

	asprintf(&url, "%s/%s", pkg_config("PACKAGESITE"),
			 pkg_get(pkg, PKG_REPOPATH));
	pkg_fetch_file(url, dest, data, cb);
	free(url);

	checksum:
	retcode = sha256_file(dest, cksum);
	if (retcode == EPKG_OK)
		if (strcmp(cksum, pkg_get(pkg, PKG_CKSUM)))
			retcode = pkg_error_set(EPKG_FATAL, "failed checksum");

	return (retcode);
}
