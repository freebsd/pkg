/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2026 Gordon Tetlow <gordon@FreeBSD.org>
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

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgsign.h"

#if OPENSSL_VERSION_NUMBER >= 0x30500000L

#define	MLDSA_ALG	"ML-DSA-87"

struct mldsa_sign_ctx {
	struct pkgsign_ctx sctx;
	EVP_PKEY *key;
};

/* Grab the mldsa context from a pkgsign_ctx. */
#define	MLDSA_CTX(c)	((struct mldsa_sign_ctx *)(c))

static int
_load_private_key(struct mldsa_sign_ctx *keyinfo)
{
	FILE *fp;

	if ((fp = fopen(keyinfo->sctx.path, "re")) == NULL)
		return (EPKG_FATAL);

	keyinfo->key = PEM_read_PrivateKey(fp, 0, keyinfo->sctx.pw_cb,
	    __DECONST(char *, keyinfo->sctx.path));
	if (keyinfo->key == NULL) {
		fclose(fp);
		return (EPKG_FATAL);
	}

	pkg_dbg(PKG_DBG_VERIFY, 1, "loaded priv key: %s", keyinfo->sctx.path);

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

struct mldsa_verify_cbdata {
	unsigned char *key;
	size_t keylen;
	unsigned char *sig;
	size_t siglen;
};

/*
 * Note that, like the rsa signer, we sign the hex encoding of the digest
 * rather than the raw digest.  This is deliberate: signing_command hands the
 * hex digest to an external signer, and a repo signed that way must verify
 * the same as one signed with a local key.
 */
static int
mldsa_verify_cb(int fd, void *ud)
{
	struct mldsa_verify_cbdata *cbdata = ud;
	char *sha512;
	char errbuf[1024];
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *ctx = NULL;
	EVP_SIGNATURE *sig_alg = NULL;
	int ret, rc = EPKG_FATAL;

	sha512 = pkg_checksum_fd(fd, PKG_HASH_TYPE_SHA512_HEX);
	if (sha512 == NULL)
		return (EPKG_FATAL);

	pkg_dbg(PKG_DBG_VERIFY, 1, "verify sha512=\"%s\"", sha512);

	pkey = _load_public_key_buf(cbdata->key, cbdata->keylen);
	if (pkey == NULL) {
		pkg_dbg(PKG_DBG_VERIFY, 1, "cannot load key");
		goto out;
	}

	ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, NULL);
	if (ctx == NULL) {
		pkg_dbg(PKG_DBG_VERIFY, 1, "cannot init context");
		goto out;
	}

	/* Specifying the algo here ensures we are using ML-DSA-87 */
	sig_alg = EVP_SIGNATURE_fetch(NULL, MLDSA_ALG, NULL);
	if (sig_alg == NULL) {
		pkg_dbg(PKG_DBG_VERIFY, 1, "cannot fetch %s: %s", MLDSA_ALG,
		    ERR_error_string(ERR_get_error(), errbuf));
		goto out;
	}

	if (EVP_PKEY_verify_message_init(ctx, sig_alg, NULL) <= 0) {
		pkg_dbg(PKG_DBG_VERIFY, 1, "cannot init verify: %s",
		    ERR_error_string(ERR_get_error(), errbuf));
		goto out;
	}

	ret = EVP_PKEY_verify(ctx, cbdata->sig, cbdata->siglen,
	    (unsigned char *)sha512,
	    pkg_checksum_type_size(PKG_HASH_TYPE_SHA512_HEX) - 1);
	if (ret <= 0) {
		if (ret < 0)
			pkg_dbg(PKG_DBG_VERIFY, 1, "verify: %s",
			    ERR_error_string(ERR_get_error(), errbuf));
		pkg_emit_error("mldsa signature verification failure");
		goto out;
	}

	rc = EPKG_OK;
out:
	EVP_SIGNATURE_free(sig_alg);
	EVP_PKEY_CTX_free(ctx);
	EVP_PKEY_free(pkey);
	free(sha512);
	return (rc);
}

static int
mldsa_verify_cert(const struct pkgsign_ctx *sctx __unused, unsigned char *key,
    size_t keylen, unsigned char *sig, size_t siglen, int fd)
{
	struct mldsa_verify_cbdata cbdata;

	(void)lseek(fd, 0, SEEK_SET);

	cbdata.key = key;
	cbdata.keylen = keylen;
	cbdata.sig = sig;
	cbdata.siglen = siglen;

	return (pkg_emit_sandbox_call(mldsa_verify_cb, fd, &cbdata));
}

static int
mldsa_verify(const struct pkgsign_ctx *sctx __unused, const char *keypath,
    unsigned char *sig, size_t sig_len, int fd)
{
	int ret;
	struct mldsa_verify_cbdata cbdata;
	char *key_buf;
	off_t key_len;

	if (file_to_buffer(keypath, &key_buf, &key_len) != EPKG_OK) {
		pkg_emit_errno("mldsa_verify", "cannot read key");
		return (EPKG_FATAL);
	}

	(void)lseek(fd, 0, SEEK_SET);

	cbdata.key = (unsigned char *)key_buf;
	cbdata.keylen = key_len;
	cbdata.sig = sig;
	cbdata.siglen = sig_len;

	ret = pkg_emit_sandbox_call(mldsa_verify_cb, fd, &cbdata);
	free(key_buf);

	return (ret);
}

static int
mldsa_sign_data(struct pkgsign_ctx *sctx, const unsigned char *msg,
    size_t msgsz, unsigned char **sigret, size_t *siglen)
{
	char errbuf[1024];
	struct mldsa_sign_ctx *keyinfo = MLDSA_CTX(sctx);
	EVP_PKEY_CTX *ctx = NULL;
	EVP_SIGNATURE *sig_alg = NULL;
	int rc = EPKG_FATAL;

	if (keyinfo->key == NULL && _load_private_key(keyinfo) != EPKG_OK) {
		pkg_emit_error("can't load key from %s", keyinfo->sctx.path);
		return (EPKG_FATAL);
	}

	ctx = EVP_PKEY_CTX_new_from_pkey(NULL, keyinfo->key, NULL);
	if (ctx == NULL)
		goto out;

	/* Specifying the algo here ensures we are using ML-DSA-87 */
	sig_alg = EVP_SIGNATURE_fetch(NULL, MLDSA_ALG, NULL);
	if (sig_alg == NULL)
		goto out;

	if (EVP_PKEY_sign_message_init(ctx, sig_alg, NULL) <= 0)
		goto out;

	/* Determine buffer length */
	if (EVP_PKEY_sign(ctx, NULL, siglen, msg, msgsz) <= 0)
		goto out;
	*sigret = xcalloc(1, *siglen);

	/* Now do the signing */
	if (EVP_PKEY_sign(ctx, *sigret, siglen, msg, msgsz) <= 0)
		goto out;

	assert(*siglen < INT_MAX);
	rc = EPKG_OK;
out:
	if (rc != EPKG_OK) {
		pkg_dbg(PKG_DBG_VERIFY, 1, "%s: %s", keyinfo->sctx.path,
		    ERR_error_string(ERR_get_error(), errbuf));
		pkg_emit_error("%s: signing failed", keyinfo->sctx.path);
	}
	EVP_SIGNATURE_free(sig_alg);
	EVP_PKEY_CTX_free(ctx);
	return (rc);
}

static int
mldsa_sign(struct pkgsign_ctx *sctx, const char *path, unsigned char **sigret,
    size_t *siglen)
{
	struct mldsa_sign_ctx *keyinfo = MLDSA_CTX(sctx);
	char *sha512;
	int ret;

	if (access(keyinfo->sctx.path, R_OK) == -1) {
		pkg_emit_errno("access", keyinfo->sctx.path);
		return (EPKG_FATAL);
	}

	sha512 = pkg_checksum_file(path, PKG_HASH_TYPE_SHA512_HEX);
	if (sha512 == NULL)
		return (EPKG_FATAL);

	pkg_dbg(PKG_DBG_VERIFY, 1, "sign file=%s sha512=%s", path, sha512);

	ret = mldsa_sign_data(sctx, (unsigned char *)sha512, strlen(sha512),
	    sigret, siglen);

	free(sha512);

	return (ret);
}

static int
mldsa_generate(struct pkgsign_ctx *sctx, const struct iovec *iov __unused,
    int niov)
{
	char errbuf[1024];
	struct mldsa_sign_ctx *keyinfo = MLDSA_CTX(sctx);
	const char *path = sctx->path;
	EVP_PKEY_CTX *ctx;
	EVP_PKEY *pkey;
	FILE *fp;
	int rc;

	if (niov != 0)
		return (EPKG_FATAL);

	fp = fopen(path, "we");
	if (fp == NULL) {
		pkg_emit_errno("fopen write", path);
		return (EPKG_FATAL);
	}

	if (fchmod(fileno(fp), 0400) != 0) {
		pkg_emit_errno("fchmod", path);
		fclose(fp);
		return (EPKG_FATAL);
	}

	pkey = NULL;
	rc = EPKG_FATAL;
	ctx = EVP_PKEY_CTX_new_from_name(NULL, MLDSA_ALG, NULL);
	if (ctx == NULL)
		goto out;

	if (EVP_PKEY_keygen_init(ctx) <= 0)
		goto out;

	if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
		goto out;

	if (PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, 0, NULL) <= 0)
		goto out;

	rc = EPKG_OK;
	if (keyinfo->key != NULL)
		EVP_PKEY_free(keyinfo->key);
	keyinfo->key = pkey;
out:
	if (rc != EPKG_OK) {
		pkg_dbg(PKG_DBG_VERIFY, 1, "%s: %s", path,
		    ERR_error_string(ERR_get_error(), errbuf));
		pkg_emit_error("%s: error generating private key", path);

		/* keyinfo claims the pkey on success for any future operations. */
		EVP_PKEY_free(pkey);
	}

	fclose(fp);
	if (rc != EPKG_OK)
		unlink(path);
	EVP_PKEY_CTX_free(ctx);
	return (rc);
}

static int
mldsa_pubkey(struct pkgsign_ctx *sctx, char **pubkey, size_t *pubkeylen)
{
	char errbuf[1024];
	struct mldsa_sign_ctx *keyinfo = MLDSA_CTX(sctx);
	BIO *bp;

	if (keyinfo->key == NULL && _load_private_key(keyinfo) != EPKG_OK) {
		pkg_emit_error("can't load key from %s", sctx->path);
		return (EPKG_FATAL);
	}

	bp = BIO_new(BIO_s_mem());
	if (bp == NULL) {
		pkg_emit_error("error allocating public key bio: %s",
		    ERR_error_string(ERR_get_error(), errbuf));
		return (EPKG_FATAL);
	}

	BIO_set_close(bp, BIO_NOCLOSE);

	if (PEM_write_bio_PUBKEY(bp, keyinfo->key) <= 0) {
		pkg_emit_error("error writing public key: %s",
		    ERR_error_string(ERR_get_error(), errbuf));
		BIO_free(bp);
		return (EPKG_FATAL);
	}

	*pubkeylen = BIO_get_mem_data(bp, pubkey);
	BIO_free(bp);
	return (EPKG_OK);
}

static pkg_checksum_type_t
mldsa_get_digest(struct pkgsign_ctx *sctx __unused)
{

	return (PKG_HASH_TYPE_SHA512_HEX);
}

static void
mldsa_free(struct pkgsign_ctx *sctx)
{
	struct mldsa_sign_ctx *keyinfo = MLDSA_CTX(sctx);

	EVP_PKEY_free(keyinfo->key);
}

const struct pkgsign_ops pkgsign_mldsa = {
	.pkgsign_ctx_size = sizeof(struct mldsa_sign_ctx),
	.pkgsign_free = mldsa_free,

	.pkgsign_sign = mldsa_sign,
	.pkgsign_verify = mldsa_verify,
	.pkgsign_verify_cert = mldsa_verify_cert,

	.pkgsign_generate = mldsa_generate,
	.pkgsign_pubkey = mldsa_pubkey,
	.pkgsign_sign_data = mldsa_sign_data,

	.pkgsign_get_digest = mldsa_get_digest,
};
#endif	/* OPENSSL_VERSION_NUMBER >= 0x30500000L */
