#include <sysexits.h>
#include <stdio.h>
#include <string.h>

#include <pkg.h>

#include "repo.h"

void
usage_repo(void)
{
	fprintf(stderr, "repo <repo-path>\n");
}

static void
progress(struct pkg *pkg, void *data) {

	char buf[BUFSIZ], pattern[BUFSIZ];
	int *len = (int *)data;
	int newlen;

	if (pkg != NULL) {
		snprintf(buf, BUFSIZ, "%s->%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		newlen = strlen(buf);

		if (newlen > *len) {
			*len = newlen;
		}

		snprintf(pattern, BUFSIZ, "\rAdding: %%-%ds", *len);

		printf(pattern, buf);
	} else {
		snprintf(pattern, BUFSIZ, "\rdone: %%-%ds", *len);
		printf(pattern, "");
	}
}

int
exec_repo(int argc, char **argv)
{
	int len=0;
	if (argc != 2) {
		usage_repo();
		return (EX_USAGE);
	}

	return (pkg_create_repo(argv[1], progress, &len));
}
