/*-
 * Copyright (c) 2021 Kyle Evans <kevans@FreeBSD.org>
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

#include <sys/cdefs.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "xmalloc.h"
#include "private/pkg.h"
#include "private/pkgsign.h"

int
pkg_key_new(struct pkg_key **key, const char *keytype, const char *keypath,
    pkg_password_cb *cb)
{
	struct pkg_key *nkey;
	struct pkgsign_ctx *ctx = NULL;
	int ret;

	assert(*key == NULL);
	assert(keytype != NULL);	/* XXX for now. */
	if (*keypath == '\0')
		return (EPKG_FATAL);

	ret = pkgsign_new_sign(keytype, &ctx);
	if (ret != 0)
		return (EPKG_FATAL);

	pkgsign_set(ctx, cb, keypath);

	nkey = xcalloc(1, sizeof(*nkey));
	nkey->ctx = ctx;

	*key = nkey;
	return (EPKG_OK);
}

void
pkg_key_free(struct pkg_key *key)
{

	pkgsign_free(key->ctx);
	free(key);
}

/*
 * Key generation callbacks may take any number of options, so we handle those
 * with an iovec.  The pkg_key layer does not discriminate, beyond enforcing
 * that options come in pairs.  The intention is that the first option in every
 * pair names the option.
 */
int
pkg_key_create(struct pkg_key *key, const struct iovec *iov, int niov)
{

	/* Malformed arguments; must come in pairs. */
	if ((niov % 2) != 0)
		return (EPKG_FATAL);

	return (pkgsign_generate(key->ctx, iov, niov));
}

int
pkg_key_sign_data(struct pkg_key *key, const unsigned char *msg, size_t msgsz,
    unsigned char **sig, size_t *siglen)
{

	return (pkgsign_sign_data(key->ctx, msg, msgsz, sig, siglen));
}

int
pkg_key_info(struct pkg_key *key, struct iovec **iov, int *niov)
{
	int rc;
	struct iovec *kiov;
	int nkiov;

	kiov = NULL;
	rc = pkgsign_keyinfo(key->ctx, &kiov, &nkiov);
	if (rc != EPKG_OK)
		return (rc);
	if ((nkiov % 2) != 0) {
		free(kiov);
		return (EPKG_FATAL);
	}

	*iov = kiov;
	*niov = nkiov;

	return (EPKG_OK);
}

int
pkg_key_pubkey(struct pkg_key *key, char **pubkey, size_t *len)
{

	return (pkgsign_pubkey(key->ctx, pubkey, len));
}
