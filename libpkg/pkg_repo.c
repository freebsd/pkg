#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
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

static RSA *
load_rsa_public_key(const char *rsa_key_path)
{
	FILE *fp;
	RSA *rsa = NULL;
	char errbuf[1024];

	if ((fp = fopen(rsa_key_path, "rb")) == 0) {
		EMIT_ERRNO("Error reading public key(%s)", rsa_key_path);
		return (NULL);
	}

	if (!PEM_read_RSA_PUBKEY( fp, &rsa, NULL, NULL )) {
		EMIT_PKG_ERROR("Error reading public key(%s): %s", rsa_key_path, ERR_error_string(ERR_get_error(), errbuf));
		fclose(fp);
		return (NULL);
	}

	fclose(fp);
	return (rsa);
}

int
pkg_repo_verify(const char *path, unsigned char *sig, unsigned int sig_len)
{
	char sha256[65];
	char errbuf[1024];
	RSA *rsa = NULL;

	sha256_file(path, sha256);

	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();

	rsa = load_rsa_public_key(pkg_config("PUBKEY"));
	if (rsa == NULL)
		return(EPKG_FATAL);

	if (RSA_verify(NID_sha1, sha256, 65, sig, sig_len, rsa) == 0) {
		EMIT_PKG_ERROR("%s: %s", pkg_config("PUBKEY"), ERR_error_string(ERR_get_error(), errbuf));
		return (EPKG_FATAL);
	}

	RSA_free(rsa);
	ERR_free_strings();

	return (EPKG_OK);
}
