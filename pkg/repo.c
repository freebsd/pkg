#include <sysexits.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include <pkg.h>

#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "repo.h"

void
usage_repo(void)
{
	fprintf(stderr, "usage: pkg repo <repo-path>\n\n");
	fprintf(stderr, "For more information see 'pkg help repo'.\n");
}

static void
progress(struct pkg *pkg, void *data)
{
	(void)data;

	if (pkg != NULL)
		printf("%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	else
		pkg_error_warn("");
}

static int
password_cb(char *buf, int size, int rwflag, void *key)
{
	int len;
	char passwd[BUFSIZ];
	(void)rwflag;

	printf("Enter pass phrase for '%s': ", (char *) key);
	scanf("%s", passwd);
	len = strlen(passwd);

	if (len <= 0)  return 0;
	if (len > size) len = size;

	memset(buf, '\0', size);
	memcpy(buf, passwd, len);
	return len;
}

static RSA *
load_rsa_private_key(char *rsa_key_path)
{
	FILE *fp;
	RSA *rsa = NULL;

	fp = fopen(rsa_key_path, "r");
	if (fp == 0) {
		return NULL;
	}

	rsa = RSA_new();
	if (rsa == NULL) {
		fclose(fp);
		return NULL;
	}

	rsa = PEM_read_RSAPrivateKey(fp, 0, password_cb, rsa_key_path);
	if (rsa == NULL) {

		fclose(fp);
		return NULL;
	}

	fclose(fp);
	return rsa;
}

int
exec_repo(int argc, char **argv)
{
	int ret;

	RSA *rsa = NULL;
	char sha256[65];
/*	char db_path[MAXPATHLEN];*/
	int max_len = 0;
	unsigned char *sigret = NULL;
	int siglen = 0;

	if (argc < 2 && argc > 3 ) {
		usage_repo();
		return (EX_USAGE);
	}

	printf("Generating repo.sqlite in %s\n", argv[1]);
	ret = pkg_create_repo(argv[1], progress, NULL, sha256);

	if (ret != EPKG_OK)
		pkg_error_warn("can not create repository");
	else
		printf("Done!\n");

	printf("sum: %s\n", sha256);

	if (argc == 3) {
		SSL_load_error_strings();

		OpenSSL_add_all_algorithms();
		OpenSSL_add_all_ciphers();


		rsa = load_rsa_private_key( argv[2] );

/*		snprintf(db_path, MAXPATHLEN, "%s/repo.sqlite", argv[1]); */

		max_len = RSA_size(rsa);
		sigret = malloc(max_len + 1);
		memset(sigret, 0, max_len);

		if (RSA_sign(NID_sha1, sha256, 65, sigret, &siglen, rsa) == 0) {
			return -1;
		}
		printf("signature: ");
		for (int i = 0; i < max_len; i++)
			 printf("%02x", sigret[i]);

		printf("\n");
		free(sigret);

		RSA_free( rsa );
		ERR_free_strings();
	}

	return (ret);
}
