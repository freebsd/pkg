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
#include <openssl/ssl.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
/*
 * This matches the historical usage for pkg.  Older versions sign the hex
 * encoding of the SHA256 checksum.  If we ever deprecated RSA, this can go
 * away.
 */
static EVP_MD *md_pkg_sha1;

static EVP_MD *
EVP_md_pkg_sha1(void)
{

	if (md_pkg_sha1 != NULL)
		return (md_pkg_sha1);

	md_pkg_sha1 = EVP_MD_meth_dup(EVP_sha1());
	if (md_pkg_sha1 == NULL)
		return (NULL);

	EVP_MD_meth_set_result_size(md_pkg_sha1,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX));
	return (md_pkg_sha1);
}
#endif	/* OPENSSL_VERSION_NUMBER >= 0x10100000L */

struct pkg_key {
	pkg_password_cb *pw_cb;
	char *path;
	EVP_PKEY *key;
};

static int
_load_private_key(struct pkg_key *keyinfo)
{
	FILE *fp;

	if ((fp = fopen(keyinfo->path, "re")) == NULL)
		return (EPKG_FATAL);

	keyinfo->key = PEM_read_PrivateKey(fp, 0, keyinfo->pw_cb, keyinfo->path);
	if (keyinfo->key == NULL) {
		fclose(fp);
		return (EPKG_FATAL);
	}

	fclose(fp);
	return (EPKG_OK);
}

static EVP_PKEY *
_load_public_key_buf(unsigned char *cert, int certlen)
{
	EVP_PKEY *pkey;
	BIO *bp;
	char errbuf[1024];

	bp = BIO_new_mem_buf((void *)cert, certlen);
	if (bp == NULL) {
		pkg_emit_error("error allocating public key bio: %s",
		    ERR_error_string(ERR_get_error(), errbuf));
		return (NULL);
	}

	pkey = PEM_read_bio_PUBKEY(bp, NULL, NULL, NULL);
	if (pkey == NULL) {
		pkg_emit_error("error reading public key: %s",
		    ERR_error_string(ERR_get_error(), errbuf));
		BIO_free(bp);
		return (NULL);
	}

	BIO_free(bp);
	return (pkey);
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
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *ctx;
	int ret;

	sha256 = pkg_checksum_fd(fd, PKG_HASH_TYPE_SHA256_HEX);
	if (sha256 == NULL)
		return (EPKG_FATAL);

	hash = pkg_checksum_data(sha256, strlen(sha256),
	    PKG_HASH_TYPE_SHA256_RAW);
	free(sha256);

	pkey = _load_public_key_buf(cbdata->key, cbdata->keylen);
	if (pkey == NULL) {
		free(hash);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_id(pkey) != EVP_PKEY_RSA) {
		EVP_PKEY_free(pkey);
		free(hash);
		return (EPKG_FATAL);
	}

	ctx = EVP_PKEY_CTX_new(pkey, NULL);
	if (ctx == NULL) {
		EVP_PKEY_free(pkey);
		free(hash);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_verify_init(ctx) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(pkey);
		free(hash);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(pkey);
		free(hash);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(pkey);
		free(hash);
		return (EPKG_FATAL);
	}

	ret = EVP_PKEY_verify(ctx, cbdata->sig, cbdata->siglen, hash,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_RAW));
	free(hash);
	if (ret <= 0) {
		if (ret < 0)
			pkg_emit_error("rsa verify failed: %s",
					ERR_error_string(ERR_get_error(), errbuf));
		else
			pkg_emit_error("rsa signature verification failure");
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(pkey);
		return (EPKG_FATAL);
	}

	EVP_PKEY_CTX_free(ctx);
	EVP_PKEY_free(pkey);

	return (EPKG_OK);
}

int
rsa_verify_cert(unsigned char *key, int keylen,
    unsigned char *sig, int siglen, int fd)
{
	int ret;
	bool need_close = false;
	struct rsa_verify_cbdata cbdata;

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
	EVP_PKEY *pkey = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
	EVP_PKEY_CTX *ctx;
#else
	RSA *rsa;
#endif
	int ret;

	sha256 = pkg_checksum_fd(fd, PKG_HASH_TYPE_SHA256_HEX);
	if (sha256 == NULL)
		return (EPKG_FATAL);

	pkey = _load_public_key_buf(cbdata->key, cbdata->keylen);
	if (pkey == NULL) {
		free(sha256);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_id(pkey) != EVP_PKEY_RSA) {
		EVP_PKEY_free(pkey);
		free(sha256);
		return (EPKG_FATAL);
	}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
	ctx = EVP_PKEY_CTX_new(pkey, NULL);
	if (ctx == NULL) {
		EVP_PKEY_free(pkey);
		free(sha256);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_verify_init(ctx) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(pkey);
		free(sha256);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(pkey);
		free(sha256);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_CTX_set_signature_md(ctx, EVP_md_pkg_sha1()) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(pkey);
		free(sha256);
		return (EPKG_FATAL);
	}

	ret = EVP_PKEY_verify(ctx, cbdata->sig, cbdata->siglen, sha256,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX));
#else
	rsa = EVP_PKEY_get1_RSA(pkey);

	ret = RSA_verify(NID_sha1, sha256,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX), cbdata->sig,
	    cbdata->siglen, rsa);
#endif
	free(sha256);
	if (ret <= 0) {
		if (ret < 0)
			pkg_emit_error("%s: %s", cbdata->key,
				ERR_error_string(ERR_get_error(), errbuf));
		else
			pkg_emit_error("%s: rsa signature verification failure",
			    cbdata->key);
#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
		EVP_PKEY_CTX_free(ctx);
#else
		RSA_free(rsa);
#endif
		EVP_PKEY_free(pkey);
		return (EPKG_FATAL);
	}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
	EVP_PKEY_CTX_free(ctx);
#else
	RSA_free(rsa);
#endif
	EVP_PKEY_free(pkey);

	return (EPKG_OK);
}

int
rsa_verify(const char *key, unsigned char *sig, unsigned int sig_len, int fd)
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
rsa_sign(char *path, struct pkg_key *keyinfo, unsigned char **sigret,
    unsigned int *osiglen)
{
	char errbuf[1024];
	int max_len = 0, ret;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
	EVP_PKEY_CTX *ctx;
	size_t siglen;
#else
	RSA *rsa;
#endif
	char *sha256;

	if (access(keyinfo->path, R_OK) == -1) {
		pkg_emit_errno("access", keyinfo->path);
		return (EPKG_FATAL);
	}

	if (keyinfo->key == NULL && _load_private_key(keyinfo) != EPKG_OK) {
		pkg_emit_error("can't load key from %s", keyinfo->path);
		return (EPKG_FATAL);
	}

	max_len = EVP_PKEY_size(keyinfo->key);
	*sigret = xcalloc(1, max_len + 1);

	sha256 = pkg_checksum_file(path, PKG_HASH_TYPE_SHA256_HEX);
	if (sha256 == NULL)
		return (EPKG_FATAL);

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
	ctx = EVP_PKEY_CTX_new(keyinfo->key, NULL);
	if (ctx == NULL) {
		free(sha256);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_sign_init(ctx) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		free(sha256);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		free(sha256);
		return (EPKG_FATAL);
	}

	if (EVP_PKEY_CTX_set_signature_md(ctx, EVP_md_pkg_sha1()) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		free(sha256);
		return (EPKG_FATAL);
	}

	siglen = max_len;
	ret = EVP_PKEY_sign(ctx, *sigret, &siglen, sha256,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX));
#else
	rsa = EVP_PKEY_get1_RSA(keyinfo->key);

	ret = RSA_sign(NID_sha1, sha256,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX),
	    *sigret, osiglen, rsa);
#endif
	free(sha256);
	if (ret <= 0) {
		pkg_emit_error("%s: %s", keyinfo->path,
		   ERR_error_string(ERR_get_error(), errbuf));
#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
		EVP_PKEY_CTX_free(ctx);
#else
		RSA_free(rsa);
#endif
		return (EPKG_FATAL);
	}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
	assert(siglen <= INT_MAX);
	*osiglen = siglen;
	EVP_PKEY_CTX_free(ctx);
#else
	RSA_free(rsa);
#endif

	return (EPKG_OK);
}

int
rsa_new(struct pkg_key **keyinfo, pkg_password_cb *cb, char *path)
{
	assert(*keyinfo == NULL);

	*keyinfo = xcalloc(1, sizeof(struct pkg_key));
	(*keyinfo)->path = path;
	(*keyinfo)->pw_cb = cb;

	SSL_load_error_strings();

	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();

	return (EPKG_OK);
}

void
rsa_free(struct pkg_key *keyinfo)
{
	if (keyinfo == NULL)
		return;

	if (keyinfo->key != NULL)
		EVP_PKEY_free(keyinfo->key);

	free(keyinfo);
	ERR_free_strings();
}

