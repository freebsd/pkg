/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/stat.h>
#include <sys/param.h>

#include <fcntl.h>

#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>


#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

struct rsa_key {
	pkg_password_cb *pw_cb;
	char *path;
	RSA *key;
};

static int
_load_rsa_private_key(struct rsa_key *rsa)
{
	FILE *fp;

	if ((fp = fopen(rsa->path, "r")) == NULL)
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
_load_rsa_public_key_buf(unsigned char *cert, int certlen)
{
	RSA *rsa = NULL;
	BIO *bp;
	char errbuf[1024];

	bp = BIO_new_mem_buf((void *)cert, certlen);
	if (!PEM_read_bio_RSA_PUBKEY(bp, &rsa, NULL, NULL)) {
		pkg_emit_error("error reading public key: %s",
		    ERR_error_string(ERR_get_error(), errbuf));
		BIO_free(bp);
		return (NULL);
	}
	BIO_free(bp);
	return (rsa);
}

struct rsa_verify_cbdata {
	unsigned char *key;
	size_t keylen;
	unsigned char *sig;
	size_t siglen;
};

static int
rsa_verify_cert_cb(int fd, void *ud)
{
	struct rsa_verify_cbdata *cbdata = ud;
	char *sha256;
	char *hash;
	char errbuf[1024];
	RSA *rsa = NULL;
	int ret;

	sha256 = pkg_checksum_fd(fd, PKG_HASH_TYPE_SHA256_HEX);
	if (sha256 == NULL)
		return (EPKG_FATAL);

	hash = pkg_checksum_data(sha256, strlen(sha256),
	    PKG_HASH_TYPE_SHA256_RAW);
	free(sha256);

	rsa = _load_rsa_public_key_buf(cbdata->key, cbdata->keylen);
	if (rsa == NULL) {
		free(hash);
		return (EPKG_FATAL);
	}
	ret = RSA_verify(NID_sha256, hash,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_RAW), cbdata->sig,
	    cbdata->siglen, rsa);
	free(hash);
	if (ret == 0) {
		pkg_emit_error("rsa verify failed: %s",
				ERR_error_string(ERR_get_error(), errbuf));
		RSA_free(rsa);
		return (EPKG_FATAL);
	}

	RSA_free(rsa);

	return (EPKG_OK);
}

int
rsa_verify_cert(const char *path, unsigned char *key, int keylen,
    unsigned char *sig, int siglen, int fd)
{
	int ret;
	bool need_close = false;
	struct rsa_verify_cbdata cbdata;

	if (fd == -1) {
		if ((fd = open(path, O_RDONLY)) == -1) {
			pkg_emit_errno("fopen", path);
			return (EPKG_FATAL);
		}
		need_close = true;
	}
	(void)lseek(fd, 0, SEEK_SET);

	cbdata.key = key;
	cbdata.keylen = keylen;
	cbdata.sig = sig;
	cbdata.siglen = siglen;

	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();

	ret = pkg_emit_sandbox_call(rsa_verify_cert_cb, fd, &cbdata);
	if (need_close)
		close(fd);

	return (ret);
}

static int
rsa_verify_cb(int fd, void *ud)
{
	struct rsa_verify_cbdata *cbdata = ud;
	char *sha256;
	char errbuf[1024];
	RSA *rsa = NULL;
	int ret;

	sha256 = pkg_checksum_fd(fd, PKG_HASH_TYPE_SHA256_HEX);
	if (sha256 == NULL)
		return (EPKG_FATAL);

	rsa = _load_rsa_public_key_buf(cbdata->key, cbdata->keylen);
	if (rsa == NULL) {
		free(sha256);
		return(EPKG_FATAL);
	}

	ret = RSA_verify(NID_sha1, sha256,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX), cbdata->sig,
	    cbdata->siglen, rsa);
	free(sha256);
	if (ret == 0) {
		pkg_emit_error("%s: %s", cbdata->key,
		    ERR_error_string(ERR_get_error(), errbuf));
		RSA_free(rsa);
		return (EPKG_FATAL);
	}

	RSA_free(rsa);

	return (EPKG_OK);
}

int
rsa_verify(const char *path, const char *key, unsigned char *sig,
    unsigned int sig_len, int fd)
{
	int ret;
	bool need_close = false;
	struct rsa_verify_cbdata cbdata;
	char *key_buf;
	off_t key_len;

	if (file_to_buffer(key, (char**)&key_buf, &key_len) != EPKG_OK) {
		pkg_emit_errno("rsa_verify", "cannot read key");
		return (EPKG_FATAL);
	}

	if (fd == -1) {
		if ((fd = open(path, O_RDONLY)) == -1) {
			pkg_emit_errno("fopen", path);
			free(key_buf);
			return (EPKG_FATAL);
		}
		need_close = true;
	}
	(void)lseek(fd, 0, SEEK_SET);

	cbdata.key = key_buf;
	cbdata.keylen = key_len;
	cbdata.sig = sig;
	cbdata.siglen = sig_len;

	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();

	ret = pkg_emit_sandbox_call(rsa_verify_cb, fd, &cbdata);
	if (need_close)
		close(fd);

	free(key_buf);

	return (ret);
}

int
rsa_sign(char *path, struct rsa_key *rsa, unsigned char **sigret, unsigned int *siglen)
{
	char errbuf[1024];
	int max_len = 0, ret;
	char *sha256;

	if (access(rsa->path, R_OK) == -1) {
		pkg_emit_errno("access", rsa->path);
		return (EPKG_FATAL);
	}

	if (rsa->key == NULL && _load_rsa_private_key(rsa) != EPKG_OK) {
		pkg_emit_error("can't load key from %s", rsa->path);
		return (EPKG_FATAL);
	}

	max_len = RSA_size(rsa->key);
	*sigret = xcalloc(1, max_len + 1);

	sha256 = pkg_checksum_file(path, PKG_HASH_TYPE_SHA256_HEX);
	if (sha256 == NULL)
		return (EPKG_FATAL);

	ret = RSA_sign(NID_sha1, sha256,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX),
	    *sigret, siglen, rsa->key);
	free(sha256);
	if (ret == 0) {
		/* XXX pass back RSA errors correctly */
		pkg_emit_error("%s: %s", rsa->path,
		   ERR_error_string(ERR_get_error(), errbuf));
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
rsa_new(struct rsa_key **rsa, pkg_password_cb *cb, char *path)
{
	assert(*rsa == NULL);

	*rsa = xcalloc(1, sizeof(struct rsa_key));
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

