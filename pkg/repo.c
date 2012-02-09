#include <sysexits.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <readpassphrase.h>

#include <pkg.h>
#include <fcntl.h>

#include "pkgcli.h"

void
usage_repo(void)
{
	fprintf(stderr, "usage: pkg repo <repo-path> <rsa-key>\n\n");
	fprintf(stderr, "For more information see 'pkg help repo'.\n");
}

static const char ps[] = { '-', '\\', '|', '/' };

static void
progress(struct pkg *pkg, void *data)
{
	int *pos;

	pos = (int *)data;

	if (*pos == 3)
		*pos = 0;
	else
		*pos = *pos + 1;

	if (pkg != NULL)
		printf("\b%c", ps[*pos]);

	fflush(stdout);
}

static int
password_cb(char *buf, int size, int rwflag, void *key)
{
	int len = 0;
	char pass[BUFSIZ];
	(void)rwflag;
	(void)key;

	if (readpassphrase("Enter passphrase: ", pass, BUFSIZ, RPP_ECHO_OFF) == NULL)
		return 0;

	len = strlen(pass);

	if (len <= 0)  return 0;
	if (len > size) len = size;

	memset(buf, '\0', size);
	memcpy(buf, pass, len);
	memset(pass, 0, BUFSIZ);

	return (len);
}

int
exec_repo(int argc, char **argv)
{
	int retcode = EPKG_OK;
	int pos = 0;
	char *rsa_key;

	if (argc < 2 || argc > 3) {
		usage_repo();
		return (EX_USAGE);
	}

	printf("Generating repo.sqlite in %s:  ", argv[1]);
	retcode = pkg_create_repo(argv[1], progress, &pos);

	if (retcode != EPKG_OK)
		printf("can not create repository");
	else
		printf("\bDone!\n");

	rsa_key = (argc == 3) ? argv[2] : NULL;
	pkg_finish_repo(argv[1], password_cb, rsa_key);

	return (retcode);
}
