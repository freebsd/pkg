/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

static int
_load_rsa_private_key(struct rsa_key *rsa)
{
	FILE *fp;

	if ((fp = fopen(rsa->path, "r")) == 0)
		return (EPKG_FATAL);

	if ((rsa->key = RSA_new()) == NULL) {
		fclose(fp);
		return (EPKG_FATAL);
	}

	rsa->key = PEM_read_RSAPrivateKey(fp, 0, rsa->pw_cb, rsa->path);
	if (rsa->key == NULL) {
		fclose(fp);
		return (EPKG_FATAL);
	}

	fclose(fp);
	return (EPKG_OK);
}

static RSA *
_load_rsa_public_key(const char *rsa_key_path)
{
	FILE *fp;
	RSA *rsa = NULL;
	char errbuf[1024];

	if ((fp = fopen(rsa_key_path, "rb")) == 0) {
		pkg_emit_errno("fopen", rsa_key_path);
		return (NULL);
	}

	if (!PEM_read_RSA_PUBKEY(fp, &rsa, NULL, NULL)) {
		pkg_emit_error("error reading public key(%s): %s", rsa_key_path,
		    ERR_error_string(ERR_get_error(), errbuf));
		fclose(fp);
		return (NULL);
	}

	fclose(fp);
	return (rsa);
}

int
rsa_verify(const char *path, const char *key, unsigned char *sig,
    unsigned int sig_len)
{
	char sha256[SHA256_DIGEST_LENGTH *2 +1];
	char errbuf[1024];
	RSA *rsa = NULL;
	int ret;

	sha256_file(path, sha256);

	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();

	rsa = _load_rsa_public_key(key);
	if (rsa == NULL)
		return(EPKG_FATAL);

	ret = RSA_verify(NID_sha1, sha256, sizeof(sha256), sig, sig_len, rsa);
	if (ret == 0) {
		pkg_emit_error("%s: %s", key,
		    ERR_error_string(ERR_get_error(), errbuf));
		return (EPKG_FATAL);
	}

	RSA_free(rsa);
	ERR_free_strings();

	return (EPKG_OK);
}

int
rsa_sign(char *path, struct rsa_key *rsa, unsigned char **sigret, unsigned int *siglen)
{
	char errbuf[1024];
	int max_len = 0, ret;
	char sha256[SHA256_DIGEST_LENGTH * 2 +1];

	if (access(rsa->path, R_OK) == -1) {
		pkg_emit_errno("access", rsa->path);
		return (EPKG_FATAL);
	}

	if (rsa->key == NULL && _load_rsa_private_key(rsa) != EPKG_OK) {
		pkg_emit_error("can't load key from %s", rsa->path);
		return (EPKG_FATAL);
	}

	max_len = RSA_size(rsa->key);
	*sigret = calloc(1, max_len + 1);

	sha256_file(path, sha256);

	ret = RSA_sign(NID_sha1, sha256, sizeof(sha256), *sigret, siglen, rsa->key);
	if (ret == 0) {
		/* XXX pass back RSA errors correctly */
		pkg_emit_error("%s: %s", rsa->path,
		   ERR_error_string(ERR_get_error(), errbuf));
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
rsa_new(struct rsa_key **rsa, pem_password_cb *cb, char *path)
{
	assert(*rsa == NULL);

	*rsa = calloc(1, sizeof(struct rsa_key));
	(*rsa)->path = path;
	(*rsa)->pw_cb = cb;

	SSL_load_error_strings();

	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();

	return (EPKG_OK);
}

void
rsa_free(struct rsa_key *rsa)
{
	if (rsa == NULL)
		return;

	if (rsa->key != NULL)
		RSA_free(rsa->key);

	free(rsa);
	ERR_free_strings();
}

