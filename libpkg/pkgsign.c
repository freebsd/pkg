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

#include "private/pkgsign.h"
#include "xmalloc.h"

/* Other parts of libpkg should use pkgsign instead of rsa directly. */
extern const struct pkgsign_ops	pkgsign_ossl;

/*
 * The eventual goal is to allow plugins to register their own pkgsign
 * implementations as needed.  The initial sketch was to add a constructor
 * to register the builtin pkgsign implementations since there should only be
 * a couple of them, but this is saved for later work.
 */
static struct pkgsign_impl {
	const char			*pi_name;
	const struct pkgsign_ops	*pi_ops;
	int				 pi_refs; /* XXX */
} pkgsign_builtins[] = {
	{
		.pi_name = "rsa",
		.pi_ops = &pkgsign_ossl,
	},
#ifdef PKGSIGN_ED25519
	{
		.pi_name = "ed25519",
		.pi_ops = &pkgsign_ossl,
	},
#endif
};

int
pkgsign_new(const char *name, struct pkgsign_ctx **ctx)
{
	struct pkgsign_impl *impl;
	const struct pkgsign_ops *ops;
	struct pkgsign_ctx *nctx;
	size_t ctx_size;
	int ret;

	assert(*ctx == NULL);

	ops = NULL;
	for (size_t i = 0; i < nitems(pkgsign_builtins); i++) {
		impl = &pkgsign_builtins[i];
		if (strcmp(name, impl->pi_name) == 0) {
			ops = impl->pi_ops;
			break;
		}
	}

	if (ops == NULL)
		return (ENOENT);

	ctx_size = ops->pkgsign_ctx_size;
	assert(ctx_size == 0 || ctx_size >= sizeof(struct pkgsign_ctx));
	if (ctx_size == 0)
		ctx_size = sizeof(struct pkgsign_ctx);

	nctx = xcalloc(1, ctx_size);
	nctx->impl = impl;

	ret = 0;
	if (ops->pkgsign_new != NULL)
		ret = (*ops->pkgsign_new)(name, nctx);

	if (ret != 0) {
		free(nctx);
		return (ret);
	}

	impl->pi_refs++;
	*ctx = nctx;
	return (0);
}

void
pkgsign_set(struct pkgsign_ctx *sctx, pkg_password_cb *cb, char *keyfile)
{

	sctx->pw_cb = cb;
	sctx->path = keyfile;
}

void
pkgsign_free(struct pkgsign_ctx *ctx)
{
	struct pkgsign_impl *impl;
	const struct pkgsign_ops *ops;

	if (ctx == NULL)
		return;
	impl = ctx->impl;
	ops = impl->pi_ops;
	if (ops->pkgsign_free != NULL)
		(*ops->pkgsign_free)(ctx);

	impl->pi_refs--;
	free(ctx);
}

int
pkgsign_sign(struct pkgsign_ctx *ctx, char *path, unsigned char **sigret,
    size_t *siglen)
{

	return (*ctx->impl->pi_ops->pkgsign_sign)(ctx, path, sigret, siglen);
}

int
pkgsign_verify(struct pkgsign_ctx *ctx, const char *key, unsigned char *sig,
    size_t siglen, int fd)
{

	return (*ctx->impl->pi_ops->pkgsign_verify)(ctx, key, sig, siglen, fd);
}

int
pkgsign_verify_cert(struct pkgsign_ctx *ctx, unsigned char *key, size_t keylen,
    unsigned char *sig, size_t siglen, int fd)
{

	return (*ctx->impl->pi_ops->pkgsign_verify_cert)(ctx, key, keylen, sig,
	    siglen, fd);
}

const char *
pkgsign_impl_name(const struct pkgsign_ctx *ctx)
{

	return (ctx->impl->pi_name);
}
