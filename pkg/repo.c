#include <sysexits.h>
#include <stdio.h>
#include <string.h>

#include <pkg.h>

#include "repo.h"

void
usage_repo(void)
{
	fprintf(stderr, "usage: pkg repo <repo-path>\n");
}

static void
progress(struct pkg *pkg, void *data) {
	(void)data;

	if (pkg != NULL)
		printf("%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	else
		pkg_error_warn("");
}

int
exec_repo(int argc, char **argv)
{
	int ret;

	if (argc != 2) {
		usage_repo();
		return (EX_USAGE);
	}

	printf("Generating repo.db in %s\n", argv[1]);
	ret = pkg_create_repo(argv[1], progress, NULL);
	printf("Done!\n");

	if (ret != EPKG_OK)
		pkg_error_warn("can not create repository");

	return (ret);
}
