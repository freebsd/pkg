/*
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

static RSA *
_load_rsa_private_key(char *rsa_key_path, pem_password_cb *password_cb)
{
	FILE *fp;
	RSA *rsa = NULL;

	if ((fp = fopen(rsa_key_path, "r")) == 0)
		return (NULL);

	if ((rsa = RSA_new()) == NULL) {
		fclose(fp);
		return (NULL);
	}

	rsa = PEM_read_RSAPrivateKey(fp, 0, password_cb, rsa_key_path);
	if (rsa == NULL) {
		fclose(fp);
		return (NULL);
	}

	fclose(fp);
	return (rsa);
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
rsa_sign(char *path, pem_password_cb *password_cb, char *rsa_key_path,
		unsigned char **sigret, unsigned int *siglen)
{
	char errbuf[1024];
	int max_len = 0, ret;
	RSA *rsa = NULL;
	char sha256[SHA256_DIGEST_LENGTH * 2 +1];

	if (access(rsa_key_path, R_OK) == -1) {
		pkg_emit_errno("access", rsa_key_path);
		return EPKG_FATAL;
	}

	SSL_load_error_strings();

	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();

	rsa = _load_rsa_private_key(rsa_key_path, password_cb);
	if (rsa == NULL) {
		pkg_emit_error("can't load key from %s", rsa_key_path);
		return EPKG_FATAL;
	}

	max_len = RSA_size(rsa);
	*sigret = calloc(1, max_len + 1);

	sha256_file(path, sha256);

	ret = RSA_sign(NID_sha1, sha256, sizeof(sha256), *sigret, siglen, rsa);
	if (ret == 0) {
		/* XXX pass back RSA errors correctly */
		pkg_emit_error("%s: %s", rsa_key_path,
					   ERR_error_string(ERR_get_error(), errbuf));
		return EPKG_FATAL;
	}

	RSA_free(rsa);
	ERR_free_strings();

	return (EPKG_OK);
}
