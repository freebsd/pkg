/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
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

#include <sys/stat.h>
#include <sys/param.h>

#include <ctype.h>
#include <fcntl.h>

#include <libder.h>

#define	WITH_STDLIB
#include <libecc/libsig.h>
#undef WITH_STDLIB

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgsign.h"

struct ecc_sign_ctx {
	struct pkgsign_ctx	sctx;
	ec_params		params;
	ec_key_pair		keypair;
	ec_alg_type		sig_alg;
	hash_alg_type		sig_hash;
	bool			loaded;
};

/* Grab the ossl context from a pkgsign_ctx. */
#define	ECC_CTX(c)	((struct ecc_sign_ctx *)(c))

#define PUBKEY_UNCOMPRESSED	0x04

#ifndef MAX
#define	MAX(a,b)	(((a)>(b))?(a):(b))
#endif

static const uint8_t oid_ecpubkey[] = \
    { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01 };

static const uint8_t oid_secp[] = \
    { 0x2b, 0x81, 0x04, 0x00 };
static const uint8_t oid_secp256k1[] = \
    { 0x2b, 0x81, 0x04, 0x00, 0x0a };
static const uint8_t oid_brainpoolP[] = \
    { 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01 };

#define	ENTRY(name, params)	{ #name, sizeof(#name) - 1, params }
static const struct pkgkey_map_entry {
	const char		*name;
	size_t			 namesz;
	const ec_str_params	*params;
} pkgkey_map[] = {
	ENTRY(WEI25519, &wei25519_str_params),
	ENTRY(SECP256K1, &secp256k1_str_params),
	ENTRY(SECP384R1, &secp384r1_str_params),
	ENTRY(SECP512R1, &secp521r1_str_params),
	ENTRY(BRAINPOOLP256R1, &brainpoolp256r1_str_params),
	ENTRY(BRAINPOOLP256T1, &brainpoolp256t1_str_params),
	ENTRY(BRAINPOOLP320R1, &brainpoolp320r1_str_params),
	ENTRY(BRAINPOOLP320T1, &brainpoolp320t1_str_params),
	ENTRY(BRAINPOOLP384R1, &brainpoolp384r1_str_params),
	ENTRY(BRAINPOOLP384T1, &brainpoolp384t1_str_params),
	ENTRY(BRAINPOOLP512R1, &brainpoolp512r1_str_params),
	ENTRY(BRAINPOOLP512T1, &brainpoolp512t1_str_params),
};

static const char pkgkey_app[] = "pkg";
static const char pkgkey_signer[] = "ecc";

static const ec_str_params *
ecc_pkgkey_params(const uint8_t *curve, size_t curvesz)
{
	const struct pkgkey_map_entry *entry;

	for (size_t i = 0; i < NELEM(pkgkey_map); i++) {
		entry = &pkgkey_map[i];
		if (curvesz != entry->namesz)
			continue;
		if (memcmp(curve, entry->name, curvesz) == 0)
			return (entry->params);
	}

	return (NULL);
}

/*
 * PKCS#8 Key:
 *     PublicKeyInfo ::= SEQUENCE {
 *       algorithm   AlgorithmIdentifier,
 *       PublicKey   BIT STRING
 *     }
 *
 *     AlgorithmIdentifier ::= SEQUENCE {
 *       algorithm   OBJECT IDENTIFIER,
 *       parameters  ANY DEFINED BY algorithm OPTIONAL
 *      }
 *
 */
/* XXX Should eventually support other kinds of keys. */
static int
ecc_pubkey_write_pkcs8(const uint8_t *keydata, size_t keysz,
    uint8_t **buf, size_t *buflen)
{
	uint8_t keybuf[EC_PUB_KEY_MAX_SIZE + 2], *outbuf;
	struct libder_ctx *ctx;
	struct libder_object *keybits, *oid, *params, *root;
	int rc;
	bool ok;

	if (keysz > sizeof(keybuf) - 2)
		return (EPKG_FATAL);

	ctx = libder_open();
	if (ctx == NULL)
		return (EPKG_FATAL);

	rc = EPKG_FATAL;
	root = libder_obj_alloc_simple(ctx, BT_SEQUENCE, NULL, 0);
	if (root == NULL)
		goto out;

	params = libder_obj_alloc_simple(ctx, BT_SEQUENCE, NULL, 0);
	if (params == NULL)
		goto out;

	ok = libder_obj_append(root, params);
	assert(ok);

	/* id-ecPublicKey */
	oid = libder_obj_alloc_simple(ctx, BT_OID, oid_ecpubkey,
	    sizeof(oid_ecpubkey));
	if (oid == NULL)
		goto out;
	ok = libder_obj_append(params, oid);
	assert(ok);

	/*
	 * secp256k1, we should eventually allow other curves and actually
	 * construct the OID.
	 */
	oid = libder_obj_alloc_simple(ctx, BT_OID, oid_secp256k1,
	    sizeof(oid_secp256k1));
	if (oid == NULL)
		goto out;
	ok = libder_obj_append(params, oid);
	assert(ok);

	memset(keybuf, 0, sizeof(keybuf));
	keybuf[0] = 0;	/* No unused bits */
	keybuf[1] = PUBKEY_UNCOMPRESSED;
	memcpy(&keybuf[2], keydata, keysz);

	keybits = libder_obj_alloc_simple(ctx, BT_BITSTRING, &keybuf[0],
	    keysz + 2);
	if (keybits == NULL)
		goto out;
	ok = libder_obj_append(root, keybits);
	assert(ok);

	/* Finally, write it out. */
	*buflen = 0;
	outbuf = libder_write(ctx, root, NULL, buflen);
	if (outbuf != NULL) {
		*buf = outbuf;
		rc = EPKG_OK;
	}

out:
	libder_obj_free(root);
	libder_close(ctx);
	return (rc);
}

/*
 * pkg key (only for EdDSA):
 *     PkgPublicKeyInfo ::= SEQUENCE {
 *       Application UTF8String
 *       Version     INTEGER
 *       Signer      UTF8String
 *       KeyType     UTF8String
 *       Public      BOOLEAN
 *       Key         BIT STRING
 *     }
 *
 * "Application" will literally contain the string: "pkg"
 * "Version" must be 1
 * "Signer" must contain the part after "pkgsign_"
 * "KeyType" is signer-defined; for ECC, it must be the curve_name
 * "Public" is self-explanatory
 * "Key" is the key data itself, encoded as the PKCS#8 public key bit string
 *   with a lead byte indicating uncompressed (0x04)
 */
static int
ecc_write_pkgkey(const ec_params *params, uint8_t public,
    const uint8_t *keydata, size_t keysz, uint8_t **buf, size_t *buflen)
{
	uint8_t keybuf[MAX(EC_PRIV_KEY_MAX_SIZE, EC_PUB_KEY_MAX_SIZE) + 2];
	uint8_t *outbuf;
	struct libder_ctx *ctx;
	struct libder_object *keybits, *obj, *root;
	int rc;
	uint8_t version = 1;
	bool ok;

	if (keysz > sizeof(keybuf) - 2)
		return (EPKG_FATAL);

	ctx = libder_open();
	if (ctx == NULL)
		return (EPKG_FATAL);

	rc = EPKG_FATAL;
	root = libder_obj_alloc_simple(ctx, BT_SEQUENCE, NULL, 0);
	if (root == NULL)
		goto out;

	/* Application */
	obj = libder_obj_alloc_simple(ctx, BT_UTF8STRING, pkgkey_app,
	    sizeof(pkgkey_app) - 1);
	if (obj == NULL)
		goto out;
	ok = libder_obj_append(root, obj);
	assert(ok);

	/* Version */
	obj = libder_obj_alloc_simple(ctx, BT_INTEGER, &version, sizeof(version));
	if (obj == NULL)
		goto out;
	ok = libder_obj_append(root, obj);
	assert(ok);

	/* Signer */
	obj = libder_obj_alloc_simple(ctx, BT_UTF8STRING, pkgkey_signer,
	    sizeof(pkgkey_signer) - 1);
	if (obj == NULL)
		goto out;
	ok = libder_obj_append(root, obj);
	assert(ok);

	/* KeyType */
	obj = libder_obj_alloc_simple(ctx, BT_UTF8STRING, params->curve_name,
	    strlen(params->curve_name));
	if (obj == NULL)
		goto out;
	ok = libder_obj_append(root, obj);
	assert(ok);

	/* Public */
	obj = libder_obj_alloc_simple(ctx, BT_BOOLEAN, &public, sizeof(public));
	if (obj == NULL)
		goto out;
	ok = libder_obj_append(root, obj);
	assert(ok);

	memset(keybuf, 0, sizeof(keybuf));
	keybuf[0] = 0;	/* No unused bits */
	keybuf[1] = PUBKEY_UNCOMPRESSED;
	memcpy(&keybuf[2], keydata, keysz);

	keybits = libder_obj_alloc_simple(ctx, BT_BITSTRING, &keybuf[0],
	    keysz + 2);
	if (keybits == NULL)
		goto out;
	ok = libder_obj_append(root, keybits);
	assert(ok);

	/* Finally, write it out. */
	*buflen = 0;
	outbuf = libder_write(ctx, root, NULL, buflen);
	if (outbuf != NULL) {
		*buf = outbuf;
		rc = EPKG_OK;
	}

out:
	libder_obj_free(root);
	libder_close(ctx);
	return (rc);
}

static int
ecc_read_pkgkey(struct libder_object *root, ec_params *params, int public,
    uint8_t *rawkey, size_t *rawlen)
{
	struct libder_object *obj;
	const uint8_t *data;
	const ec_str_params *sparams;
	size_t datasz;
	int ret;

	if (libder_obj_type_simple(root) != BT_SEQUENCE)
		return (EPKG_FATAL);

	/* Application */
	obj = libder_obj_child(root, 0);
	if (obj == NULL || libder_obj_type_simple(obj) != BT_UTF8STRING)
		return (EPKG_FATAL);
	data = libder_obj_data(obj, &datasz);
	if (datasz != sizeof(pkgkey_app) - 1 ||
	    memcmp(data, pkgkey_app, datasz) != 0)
		return (EPKG_FATAL);

	/* Version */
	obj = libder_obj_child(root, 1);
	if (obj == NULL || libder_obj_type_simple(obj) != BT_INTEGER)
		return (EPKG_FATAL);
	data = libder_obj_data(obj, &datasz);
	if (datasz != 1 || *data != 1 /* XXX */)
		return (EPKG_FATAL);

	/* Signer */
	obj = libder_obj_child(root, 2);
	if (obj == NULL || libder_obj_type_simple(obj) != BT_UTF8STRING)
		return (EPKG_FATAL);
	data = libder_obj_data(obj, &datasz);
	if (datasz != sizeof(pkgkey_signer) - 1 ||
	    memcmp(data, pkgkey_signer, datasz) != 0)
		return (EPKG_FATAL);

	/* KeyType (curve) */
	obj = libder_obj_child(root, 3);
	if (obj == NULL || libder_obj_type_simple(obj) != BT_UTF8STRING)
		return (EPKG_FATAL);
	data = libder_obj_data(obj, &datasz);
	sparams = ecc_pkgkey_params(data, datasz);
	if (sparams == NULL)
		return (EPKG_FATAL);

	ret = import_params(params, sparams);
	if (ret != 0)
		return (EPKG_FATAL);

	/* Public? */
	obj = libder_obj_child(root, 4);
	if (obj == NULL || libder_obj_type_simple(obj) != BT_BOOLEAN)
		return (EPKG_FATAL);
	data = libder_obj_data(obj, &datasz);
	if (datasz != 1 || !data[0] != !public)
		return (EPKG_FATAL);

	/* Key */
	obj = libder_obj_child(root, 5);
	if (obj == NULL || libder_obj_type_simple(obj) != BT_BITSTRING)
		return (EPKG_FATAL);
	data = libder_obj_data(obj, &datasz);
	if (datasz <= 2 || data[0] != 0 || data[1] != PUBKEY_UNCOMPRESSED)
		return (EPKG_FATAL);

	data += 2;
	datasz -= 2;

	if (datasz > *rawlen)
		return (EPKG_FATAL);


	memcpy(rawkey, data, datasz);
	*rawlen = datasz;

	return (EPKG_OK);
}

static int
ecc_write_signature_component(struct libder_ctx *ctx, struct libder_object *root,
    const uint8_t *sigpart, size_t partlen)
{
	uint8_t sigbounce[EC_MAX_SIGLEN];
	struct libder_object *obj;
	size_t curlen;
	bool ok;

	/*
	 * If we need a leading 0 because the sign bit is set, we may need to
	 * bounce through sigbounce.  We may also need to bounce if there's some
	 * leading zeros.
	 */
	curlen = partlen;
	while (curlen > 0 && sigpart[0] == 0) {
		curlen--;
		sigpart++;
	}

	if ((sigpart[0] & 0x80) != 0) {
		/*
		 * If the high bit is set, we need to bounce it through
		 * sigbounce and insert a leading 0.
		 */
		sigbounce[0] = 0;
		memcpy(&sigbounce[1], sigpart, curlen);

		obj = libder_obj_alloc_simple(ctx, BT_INTEGER, sigbounce,
		    curlen + 1);
	} else {
		/*
		 * Otherwise, we can just leave it be.
		 */

		obj = libder_obj_alloc_simple(ctx, BT_INTEGER, sigpart, curlen);
	}

	if (obj == NULL)
		return (EPKG_FATAL);

	ok = libder_obj_append(root, obj);
	assert(ok);
	return (EPKG_OK);
}

/*
 *       Ecdsa-Sig-Value  ::=  SEQUENCE  {
 *            r     INTEGER,
 *            s     INTEGER  }
 */
static int
ecc_write_signature(const uint8_t *sig, size_t siglen, uint8_t **sigret,
    size_t *sigretlen)
{
	struct libder_ctx *ctx;
	struct libder_object *obj, *root;
	uint8_t *outbuf;
	size_t complen;
	int rc;

	ctx = libder_open();
	if (ctx == NULL)
		return (EPKG_FATAL);

	rc = EPKG_FATAL;
	obj = NULL;
	root = libder_obj_alloc_simple(ctx, BT_SEQUENCE, NULL, 0);
	if (root == NULL)
		goto out;

	complen = siglen / 2;
	rc = ecc_write_signature_component(ctx, root, sig, complen);
	if (rc != EPKG_OK)
		goto out;

	sig += complen;
	siglen -= complen;
	rc = ecc_write_signature_component(ctx, root, sig, complen);
	if (rc != EPKG_OK)
		goto out;

	*sigretlen = 0;
	outbuf = libder_write(ctx, root, NULL, sigretlen);
	if (outbuf != NULL) {
		*sigret = outbuf;
		rc = EPKG_OK;
	}
out:
	libder_obj_free(obj);
	libder_close(ctx);
	return (rc);
}

static int
ecc_extract_signature(const uint8_t *sig, size_t siglen, uint8_t *rawsig,
    size_t rawlen)
{
	struct libder_ctx *ctx;
	struct libder_object *obj, *root;
	const uint8_t *sigdata;
	size_t compsz, datasz, sigoff;
	int rc;

	ctx = libder_open();
	if (ctx == NULL)
		return (EPKG_FATAL);

	rc = EPKG_FATAL;
	root = libder_read(ctx, sig, &siglen);
	if (root == NULL || libder_obj_type_simple(root) != BT_SEQUENCE)
		goto out;

	/* Descend into the sequence's payload, extract both numbers. */
	compsz = rawlen / 2;
	sigoff = 0;
	for (int i = 0; i < 2; i++) {
		obj = libder_obj_child(root, i);
		if (libder_obj_type_simple(obj) != BT_INTEGER)
			goto out;

		sigdata = libder_obj_data(obj, &datasz);
		if (datasz < 2 || datasz > compsz + 1)
			goto out;

		/*
		 * We may see an extra lead byte if our high bit of the first
		 * byte was set, since these numbers are positive by definition.
		 */
		if (sigdata[0] == 0 && (sigdata[1] & 0x80) != 0) {
			sigdata++;
			datasz--;
		}

		/* Sanity check: don't overflow the output. */
		if (sigoff + datasz > rawlen)
			goto out;

		/* Padding to the significant end if we're too small. */
		if (datasz < compsz) {
			memset(&rawsig[sigoff], 0, compsz - datasz);
			sigoff += compsz - datasz;
		}

		memcpy(&rawsig[sigoff], sigdata, datasz);
		sigoff += datasz;
	}

	/* Sanity check: must have exactly the required # of signature bits. */
	rc = (sigoff == rawlen) ? EPKG_OK : EPKG_FATAL;

out:
	libder_obj_free(root);
	libder_close(ctx);
	return (rc);
}

static int
ecc_extract_pubkey_string(const uint8_t *data, size_t datalen, uint8_t *rawkey,
    size_t *rawlen)
{
	uint8_t prefix, usebit;

	if (datalen <= 2)
		return (EPKG_FATAL);

	usebit = *data++;
	datalen--;

	if (usebit != 0)
		return (EPKG_FATAL);

	prefix = *data++;
	datalen--;

	if (prefix != PUBKEY_UNCOMPRESSED)
		return (EPKG_FATAL);

	if (datalen > *rawlen)
		return (EPKG_FATAL);

	memcpy(rawkey, data, datalen);
	*rawlen = datalen;

	return (EPKG_OK);
}

static int
ecc_extract_key_params(const uint8_t *oid, size_t oidlen,
    ec_params *rawparams)
{
	int ret;

	if (oidlen >= sizeof(oid_secp) &&
	    memcmp(oid, oid_secp, sizeof(oid_secp)) >= 0) {
		oid += sizeof(oid_secp);
		oidlen -= sizeof(oid_secp);

		if (oidlen != 1)
			return (EPKG_FATAL);

		ret = -1;
		switch (*oid) {
		case 0x0a:	/* secp256k1 */
			ret = import_params(rawparams, &secp256k1_str_params);
			break;
		case 0x22:	/* secp384r1 */
			ret = import_params(rawparams, &secp384r1_str_params);
			break;
		case 0x23:	/* secp521r1 */
			ret = import_params(rawparams, &secp521r1_str_params);
			break;
		default:
			return (EPKG_FATAL);
		}

		if (ret == 0)
			return (EPKG_OK);
		return (EPKG_FATAL);
	}

	if (oidlen >= sizeof(oid_brainpoolP) &&
	    memcmp(oid, oid_brainpoolP, sizeof(oid_brainpoolP)) >= 0) {
		oid += sizeof(oid_brainpoolP);
		oidlen -= sizeof(oid_brainpoolP);

		if (oidlen != 1)
			return (EPKG_FATAL);

		ret = -1;
		switch (*oid) {
		case 0x07:	/* brainpoolP256r1 */
			ret = import_params(rawparams, &brainpoolp256r1_str_params);
			break;
		case 0x08:	/* brainpoolP256t1 */
			ret = import_params(rawparams, &brainpoolp256t1_str_params);
			break;
		case 0x09:	/* brainpoolP320r1 */
			ret = import_params(rawparams, &brainpoolp320r1_str_params);
			break;
		case 0x0a:	/* brainpoolP320t1 */
			ret = import_params(rawparams, &brainpoolp320t1_str_params);
			break;
		case 0x0b:	/* brainpoolP384r1 */
			ret = import_params(rawparams, &brainpoolp384r1_str_params);
			break;
		case 0x0c:	/* brainpoolP384t1 */
			ret = import_params(rawparams, &brainpoolp384t1_str_params);
			break;
		case 0x0d:	/* brainpoolP512r1 */
			ret = import_params(rawparams, &brainpoolp512r1_str_params);
			break;
		case 0x0e:	/* brainpoolP512t1 */
			ret = import_params(rawparams, &brainpoolp512t1_str_params);
			break;
		default:
			return (EPKG_FATAL);
		}

		if (ret == 0)
			return (EPKG_OK);
		return (EPKG_FATAL);
	}

#ifdef ECC_DEBUG
	for (size_t i = 0; i < oidlen; i++) {
		fprintf(stderr, "%.02x ", oid[i]);
	}

	fprintf(stderr, "\n");
#endif

	return (EPKG_FATAL);
}

/*
 * On entry, *rawparams should point to an ec_params that we can import the
 * key parameters to.  We'll either do that, or we'll set it to NULL if we could
 * not deduce the curve.
 */
static int
ecc_extract_pubkey(const uint8_t *key, size_t keylen, uint8_t *rawkey,
    size_t *rawlen, ec_params *rawparams)
{
	const uint8_t *oidp;
	struct libder_ctx *ctx;
	struct libder_object *keydata, *oid, *params, *root;
	size_t oidsz;
	int rc;

	ctx = libder_open();
	if (ctx == NULL)
		return (EPKG_FATAL);

	rc = EPKG_FATAL;
	root = libder_read(ctx, key, &keylen);
	if (root == NULL || libder_obj_type_simple(root) != BT_SEQUENCE)
		goto out;

	params = libder_obj_child(root, 0);

	if (params == NULL) {
		goto out;
	} else if (libder_obj_type_simple(params) != BT_SEQUENCE) {
		rc = ecc_read_pkgkey(root, rawparams, 1, rawkey, rawlen);
		goto out;
	}

	/* Is a sequence */
	keydata = libder_obj_child(root, 1);
	if (keydata == NULL || libder_obj_type_simple(keydata) != BT_BITSTRING)
		goto out;

	/* Key type */
	oid = libder_obj_child(params, 0);
	if (oid == NULL || libder_obj_type_simple(oid) != BT_OID)
		goto out;

	oidp = libder_obj_data(oid, &oidsz);
	if (oidsz != sizeof(oid_ecpubkey) ||
	    memcmp(oidp, oid_ecpubkey, oidsz) != 0)
		return (EPKG_FATAL);

	/* Curve */
	oid = libder_obj_child(params, 1);
	if (oid == NULL || libder_obj_type_simple(oid) != BT_OID)
		goto out;

	oidp = libder_obj_data(oid, &oidsz);
	if (ecc_extract_key_params(oidp, oidsz, rawparams) != EPKG_OK)
		goto out;

	/* Finally, peel off the key material */
	key = libder_obj_data(keydata, &keylen);
	if (ecc_extract_pubkey_string(key, keylen, rawkey, rawlen) != EPKG_OK)
		goto out;

	rc = EPKG_OK;
out:
	libder_obj_free(root);
	libder_close(ctx);
	return (rc);
}

static int
ecc_extract_privkey(const uint8_t *key, size_t keylen, uint8_t *rawkey,
    size_t *rawlen, ec_params *rawparams)
{
	const uint8_t *data;
	struct libder_ctx *ctx;
	struct libder_object *obj, *root;
	size_t datasz;
	int rc;

	ctx = libder_open();
	if (ctx == NULL)
		return (EPKG_FATAL);

	rc = EPKG_FATAL;
	root = libder_read(ctx, key, &keylen);
	if (root == NULL || libder_obj_type_simple(root) != BT_SEQUENCE)
		goto out;

	/* Sanity check the version, we're expecting version 1. */
	obj = libder_obj_child(root, 0);
	if (obj == NULL)
		goto out;
	if (libder_obj_type_simple(obj) != BT_INTEGER) {
		rc = ecc_read_pkgkey(root, rawparams, 0, rawkey, rawlen);
		goto out;
	}

	data = libder_obj_data(obj, &datasz);
	if (datasz != 1 || *data != 1)
		goto out;

	/* Grab the key data itself. */
	obj = libder_obj_child(root, 1);
	if (obj == NULL || libder_obj_type_simple(obj) != BT_OCTETSTRING)
		goto out;

	data = libder_obj_data(obj, &datasz);
	if (datasz == 0 || datasz > *rawlen)
		goto out;

	memcpy(rawkey, data, datasz);
	*rawlen = datasz;

	/* Next, extract the OID describing the key type. */
	obj = libder_obj_child(root, 2);
	if (obj == NULL || libder_obj_type_simple(obj) !=
	    ((BC_CONTEXT << 6) | BER_TYPE_CONSTRUCTED_MASK))
		goto out;

	obj = libder_obj_child(obj, 0);
	if (obj == NULL || libder_obj_type_simple(obj) != BT_OID)
		goto out;

	data = libder_obj_data(obj, &datasz);
	if (ecc_extract_key_params(data, datasz, rawparams) != EPKG_OK)
		goto out;

	rc = EPKG_OK;
out:
	libder_obj_free(root);
	libder_close(ctx);
	return (rc);
}

static int
_generate_private_key(struct ecc_sign_ctx *keyinfo)
{
	int ret;

	ret = ec_key_pair_gen(&keyinfo->keypair, &keyinfo->params,
	    keyinfo->sig_alg);

	if (ret != 0) {
		pkg_emit_error("failed to generate ecc keypair");
		return (EPKG_FATAL);
	}

	keyinfo->loaded = true;
	return (EPKG_OK);
}

static int
_load_private_key(struct ecc_sign_ctx *keyinfo)
{
	struct stat st;
	uint8_t keybuf[EC_PRIV_KEY_MAX_SIZE];
	uint8_t *filedata;
	ssize_t readsz;
	size_t keysz;
	int fd, rc, ret;
	size_t offset, resid;

	filedata = NULL;
	fd = -1;
	rc = EPKG_FATAL;

	keyinfo->loaded = false;
	if ((fd = open(keyinfo->sctx.path, O_RDONLY)) == -1)
		return (EPKG_FATAL);

	if (fstat(fd, &st) == -1)
		goto out;

	filedata = xmalloc(st.st_size);
	resid = st.st_size;
	offset = 0;
	while (resid != 0) {
		readsz = read(fd, &filedata[offset], resid);
		if (readsz <= 0)
			break;
		resid -= readsz;
		offset += readsz;
	}

	if (readsz < 0) {
		pkg_emit_errno("read", keyinfo->sctx.path);
		goto out;
	} else if (resid != 0) {
		pkg_emit_error("%s: failed to read key",
		    keyinfo->sctx.path);
		goto out;
	}

	/*
	 * Try DER-decoding it.  Unlike with loading a pubkey, anything
	 * requiring the privkey requires a new context for each operation, so
	 * we can just clobber keyinfo->params at will.
	 */
	keysz = sizeof(keybuf);
	if (ecc_extract_privkey(filedata, offset, keybuf, &keysz,
	    &keyinfo->params) != EPKG_OK) {
		pkg_emit_error("failed to decode private key");
		goto out;
	}

	ret = ec_priv_key_import_from_buf(&keyinfo->keypair.priv_key,
	    &keyinfo->params, keybuf, keysz, keyinfo->sig_alg);
	if (ret == 0) {
		ret = init_pubkey_from_privkey(&keyinfo->keypair.pub_key,
			&keyinfo->keypair.priv_key);
		if (ret == 0) {
			keyinfo->loaded = true;
			rc = EPKG_OK;
		} else {
			pkg_emit_error("%s: failed to derive public key",
			    keyinfo->sctx.path);
			rc = EPKG_FATAL;
		}
	} else {
		pkg_emit_error("%s: failed to import private key",
		    keyinfo->sctx.path);
		rc = EPKG_FATAL;
	}

out:
	free(filedata);
	if (fd != -1)
		close(fd);
	return (rc);
}

struct ecc_verify_cbdata {
	const struct pkgsign_ctx *sctx;
	unsigned char *key;
	size_t keylen;
	unsigned char *sig;
	size_t siglen;
};

static int
ecc_verify_internal(struct ecc_verify_cbdata *cbdata, const uint8_t *hash,
    size_t hashsz)
{
	ec_pub_key pubkey;
	ec_params derparams;
	struct ecc_sign_ctx *keyinfo = ECC_CTX(cbdata->sctx);
	uint8_t keybuf[EC_PUB_KEY_MAX_SIZE];
	uint8_t rawsig[EC_MAX_SIGLEN];
	size_t keysz;
	int ret;
	uint8_t ecsiglen;

	keysz = MIN(sizeof(keybuf), cbdata->keylen / 2);

	keysz = sizeof(keybuf);
	if (ecc_extract_pubkey(cbdata->key, cbdata->keylen, keybuf,
	    &keysz, &derparams) != EPKG_OK) {
		pkg_emit_error("failed to parse key");
		return (EPKG_FATAL);
	}

	ret = ec_get_sig_len(&derparams, keyinfo->sig_alg, keyinfo->sig_hash,
	    &ecsiglen);
	if (ret != 0)
		return (EPKG_FATAL);

	/*
	 * Signatures are DER-encoded, whether by OpenSSL or pkg.
	 */
	if (ecc_extract_signature(cbdata->sig, cbdata->siglen,
	    rawsig, ecsiglen) != EPKG_OK) {
		pkg_emit_error("failed to decode signature");
		return (EPKG_FATAL);
	}

	ret = ec_pub_key_import_from_aff_buf(&pubkey, &derparams,
	    keybuf, keysz, keyinfo->sig_alg);
	if (ret != 0) {
		pkg_emit_error("failed to import key");
		return (EPKG_FATAL);
	}

	ret = ec_verify(rawsig, ecsiglen, &pubkey, hash, hashsz, keyinfo->sig_alg,
	    keyinfo->sig_hash, NULL, 0);
	if (ret != 0) {
		pkg_emit_error("failed to verify signature");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}


static int
ecc_verify_cert_cb(int fd, void *ud)
{
	struct ecc_verify_cbdata *cbdata = ud;
	char *sha256;
	int ret;

	sha256 = pkg_checksum_fd(fd, PKG_HASH_TYPE_SHA256_HEX);
	if (sha256 == NULL)
		return (EPKG_FATAL);

	ret = ecc_verify_internal(cbdata, sha256, strlen(sha256));
	if (ret != 0) {
		pkg_emit_error("ecc signature verification failure");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
ecc_verify_cert(const struct pkgsign_ctx *sctx, unsigned char *key,
    size_t keylen, unsigned char *sig, size_t siglen, int fd)
{
	int ret;
	struct ecc_verify_cbdata cbdata;

	(void)lseek(fd, 0, SEEK_SET);

	cbdata.sctx = sctx;
	cbdata.key = key;
	cbdata.keylen = keylen;
	cbdata.sig = sig;
	cbdata.siglen = siglen;

	ret = pkg_emit_sandbox_call(ecc_verify_cert_cb, fd, &cbdata);

	return (ret);
}

static int
ecc_verify_cb(int fd, void *ud)
{
	struct ecc_verify_cbdata *cbdata = ud;
	uint8_t *blake2;
	int ret;

	blake2 = pkg_checksum_fd(fd, PKG_HASH_TYPE_BLAKE2_RAW);
	if (blake2 == NULL)
		return (EPKG_FATAL);

	ret = ecc_verify_internal(cbdata, blake2,
	    pkg_checksum_type_size(PKG_HASH_TYPE_BLAKE2_RAW));

	free(blake2);
	if (ret != 0) {
		pkg_emit_error("ecc signature verification failure");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
ecc_verify(const struct pkgsign_ctx *sctx, const char *keypath,
    unsigned char *sig, size_t sig_len, int fd)
{
	int ret;
	struct ecc_verify_cbdata cbdata;
	char *key_buf;
	off_t key_len;

	if (file_to_buffer(keypath, (char**)&key_buf, &key_len) != EPKG_OK) {
		pkg_emit_errno("ecc_verify", "cannot read key");
		return (EPKG_FATAL);
	}

	(void)lseek(fd, 0, SEEK_SET);

	cbdata.sctx = sctx;
	cbdata.key = key_buf;
	cbdata.keylen = key_len;
	cbdata.sig = sig;
	cbdata.siglen = sig_len;

	ret = pkg_emit_sandbox_call(ecc_verify_cb, fd, &cbdata);

	free(key_buf);

	return (ret);
}

static int
ecc_sign_data(struct pkgsign_ctx *sctx, const unsigned char *msg, size_t msgsz,
    unsigned char **sigret, size_t *siglen)
{
	uint8_t rawsig[EC_MAX_SIGLEN];
	struct ecc_sign_ctx *keyinfo = ECC_CTX(sctx);
	int ret;
	uint8_t rawlen;

	if (!keyinfo->loaded && _load_private_key(keyinfo) != EPKG_OK) {
		pkg_emit_error("%s: failed to load key", keyinfo->sctx.path);
		return (EPKG_FATAL);
	}

	ret = ec_get_sig_len(&keyinfo->params, keyinfo->sig_alg, keyinfo->sig_hash,
		&rawlen);
	if (ret != 0)
		return (EPKG_FATAL);

	assert(rawlen <= sizeof(rawsig));

	assert(priv_key_check_initialized_and_type(&keyinfo->keypair.priv_key,
	    keyinfo->sig_alg) == 0);
	assert(pub_key_check_initialized_and_type(&keyinfo->keypair.pub_key,
	    keyinfo->sig_alg) == 0);

	ret = ec_sign(rawsig, rawlen, &keyinfo->keypair, msg, msgsz,
	    keyinfo->sig_alg, keyinfo->sig_hash, NULL, 0);

	if (ret != 0) {
		pkg_emit_error("%s: ecc signing failure", keyinfo->sctx.path);
		return (EPKG_FATAL);
	}

	if (ecc_write_signature(rawsig, rawlen, sigret, siglen) != EPKG_OK) {
		pkg_emit_error("failed to encode signature");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
ecc_sign(struct pkgsign_ctx *sctx, const char *path, unsigned char **sigret,
    size_t *siglen)
{
	uint8_t *blake2;
	struct ecc_sign_ctx *keyinfo = ECC_CTX(sctx);
	int ret;

	if (access(keyinfo->sctx.path, R_OK) == -1) {
		pkg_emit_errno("access", keyinfo->sctx.path);
		return (EPKG_FATAL);
	}

	blake2 = pkg_checksum_file(path, PKG_HASH_TYPE_BLAKE2_RAW);
	if (blake2 == NULL)
		return (EPKG_FATAL);

	ret = ecc_sign_data(sctx, blake2,
	    pkg_checksum_type_size(PKG_HASH_TYPE_BLAKE2_RAW), sigret, siglen);
	free(blake2);

	return (ret);
}

static int
ecc_generate(struct pkgsign_ctx *sctx, const struct iovec *iov __unused,
    int niov __unused)
{
	uint8_t keybuf[EC_PRIV_KEY_MAX_SIZE], *outbuf;
	struct ecc_sign_ctx *keyinfo = ECC_CTX(sctx);
	const char *path = sctx->path;
	FILE *fp;
	size_t keysz, outsz;

	if (niov != 0)
		return (EPKG_FATAL);

	if (_generate_private_key(keyinfo) != 0)
		return (EPKG_FATAL);

	assert(priv_key_check_initialized_and_type(&keyinfo->keypair.priv_key,
	    keyinfo->sig_alg) == 0);
	assert(pub_key_check_initialized_and_type(&keyinfo->keypair.pub_key,
	    keyinfo->sig_alg) == 0);

	keysz = EC_PRIV_KEY_EXPORT_SIZE(&keyinfo->keypair.priv_key);
	if (ec_priv_key_export_to_buf(&keyinfo->keypair.priv_key,
	    keybuf, keysz) != 0) {
		pkg_emit_error("failed to export ecc key");
		return (EPKG_FATAL);
	}

	outbuf = NULL;
	outsz = 0;
	if (ecc_write_pkgkey(&keyinfo->params, 0, keybuf, keysz, &outbuf,
	    &outsz) != EPKG_OK) {
		pkg_emit_error("%s: failed to write DER-encoded key",
		    sctx->path);
		return (EPKG_FATAL);
	}

	fp = fopen(path, "wb");
	if (fp == NULL) {
		pkg_emit_errno("fopen write", path);
		free(outbuf);
		return (EPKG_FATAL);
	}

	if (fchmod(fileno(fp), 0400) != 0) {
		pkg_emit_errno("fchmod", path);
		free(outbuf);
		fclose(fp);
		return (EPKG_FATAL);
	}

	fwrite(outbuf, outsz, 1, fp);
	free(outbuf);
	outbuf = NULL;
	if (ferror(fp) != 0 || fflush(fp) != 0) {
		pkg_emit_errno("fwrite", path);
		fclose(fp);
		return (EPKG_FATAL);
	}

	fclose(fp);
	return (EPKG_OK);
}

static int
ecc_pubkey(struct pkgsign_ctx *sctx, char **pubkey, size_t *pubkeylen)
{
	struct ecc_sign_ctx *keyinfo = ECC_CTX(sctx);
	uint8_t keybuf[EC_PUB_KEY_MAX_SIZE];
	size_t keylen;
	int ret;

	if (!keyinfo->loaded && _load_private_key(keyinfo) != EPKG_OK) {
		pkg_emit_error("%s: failed to load key", sctx->path);
		return (EPKG_FATAL);
	}

	assert(keyinfo->loaded);
	assert(pub_key_check_initialized_and_type(&keyinfo->keypair.pub_key,
	    keyinfo->sig_alg) == 0);

	keylen = 2 * BYTECEIL(keyinfo->params.ec_fp.p_bitlen);
	ret = ec_pub_key_export_to_aff_buf(&keyinfo->keypair.pub_key, keybuf, keylen);
	if (ret != 0) {
		pkg_emit_error("%s: failed to export key", sctx->path);
		return (EPKG_FATAL);
	}

	/*
	 * We'll write a custom format for anything but ECDSA that includes our
	 * params wholesale so that we can just import them directly.
	 *
	 * ECDSA keys get exported as PKCS#8 for interoperability with OpenSSL.
	 */
	if (keyinfo->sig_alg != ECDSA) {
		if (ecc_write_pkgkey(&keyinfo->params, 1, keybuf, keylen,
		    (uint8_t **)pubkey, pubkeylen) != EPKG_OK) {
			pkg_emit_error("%s: failed to write DER-encoded key",

			    sctx->path);
			return (EPKG_FATAL);
		}
	} else if (ecc_pubkey_write_pkcs8(keybuf, keylen,
	    (uint8_t **)pubkey, pubkeylen) != EPKG_OK) {
		pkg_emit_error("%s: failed to write DER-encoded key", sctx->path);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
ecc_new(const char *name __unused, struct pkgsign_ctx *sctx)
{
	struct ecc_sign_ctx *keyinfo = ECC_CTX(sctx);
	int ret;

	ret = EPKG_FATAL;
	if (strcmp(name, "ecc") == 0 || strcmp(name, "eddsa") == 0) {
			keyinfo->sig_alg = EDDSA25519;
			keyinfo->sig_hash = SHA512;
			ret = import_params(&keyinfo->params, &wei25519_str_params);
	} else if (strcmp(name, "ecdsa") == 0) {
			keyinfo->sig_alg = ECDSA;
			keyinfo->sig_hash = SHA256;
			ret = import_params(&keyinfo->params, &secp256k1_str_params);
	}

	if (ret != 0)
		return (EPKG_FATAL);

	return (0);
}

static void
ecc_free(struct pkgsign_ctx *sctx __unused)
{

}

const struct pkgsign_ops pkgsign_ecc = {
	.pkgsign_ctx_size = sizeof(struct ecc_sign_ctx),
	.pkgsign_new = ecc_new,
	.pkgsign_free = ecc_free,

	.pkgsign_sign = ecc_sign,
	.pkgsign_verify = ecc_verify,
	.pkgsign_verify_cert = ecc_verify_cert,

	.pkgsign_generate = ecc_generate,
	.pkgsign_pubkey = ecc_pubkey,
	.pkgsign_sign_data = ecc_sign_data,
};
