/*-
 * Copyright (c) 2026 Kyle Evans <kevans@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libder_private.h"

static size_t
arc_encoded_len(uint64_t oval)
{
	int nbits;

	if (oval == 0)
		return (1);

	nbits = 64 - __builtin_clzll(oval);
	return ((nbits + 6) / 7);
}

static bool
arc_encode(FILE *fp, uint64_t oval)
{
	off_t fpos;
	size_t writesz;
	uint8_t arcb;

#define oid_write(b) do {			\
	arcb = (b);				\
	writesz = fwrite(&arcb, 1, 1, fp);	\
	if (writesz != 1)			\
		return (false);			\
} while(0)

	/*
	 * Base-128 Encoding: calculate how many bytes are needed for
	 * the representation and extend our stream in advance, then
	 * work backwards.
	 */
	fseek(fp, arc_encoded_len(oval) - 1, SEEK_CUR);
	fpos = ftello(fp);
	oid_write(oval & 0x7f);
	oval >>= 7;

	while (oval != 0) {
		fseek(fp, -2, SEEK_CUR);
		oid_write(0x80 | (oval & 0x7f));
		oval >>= 7;
	}

	fseeko(fp, fpos + 1, SEEK_SET);
#undef oid_write
	return (true);
}

static bool
oid_root_sane(uint64_t root, uint64_t sec)
{

	switch (root) {
	case 0:
	case 1:
		/* Limited to 0-40 do to our (root * 40) + sec formula. */
		return (sec < 40);
	case 2:
		/* Just can't overflow UINT64_MAX */
		return (sec <= (UINT64_MAX - (root * 40)));
	default:
		break;
	}

	return (false);
}

uint8_t *
libder_oid_parse(const char *oidstr, size_t *outsz)
{
	unsigned long long root, sec;
	FILE *oidf;
	char *buf;
	size_t oidsz;
	int order = 0, serrno;

	/* X.690 Encoding */
	if (sscanf(oidstr, "%llu.%llu", &root, &sec) != 2) {
		errno = EINVAL;
		return (NULL);
	} else if (!oid_root_sane(root, sec)) {
		/* Covers overflow and invalid root values. */
		errno = EINVAL;
		return (NULL);
	}

	oidf = open_memstream(&buf, &oidsz);
	if (oidf == NULL)
		return (NULL);

	/*
	 * This will usually just be a single byte, but technically it can be
	 * larger and must also be base-128 encoded.
	 */
	if (!arc_encode(oidf, (root * 40) + sec))
		goto failed;

	for (size_t start = 0, nsep = strcspn(oidstr, "."); oidstr[start] != '\0';
	    start = oidstr[nsep] == '\0' ? nsep : nsep + 1,
	    nsep = start + strcspn(&oidstr[start], ".")) {
		char *endp;
		unsigned long long oval;

		/*
		 * We're checking before skipping the first two components
		 * because sscanf will happily accept a sign, so we just want to
		 * additionally validate the first two.
		 */
		if (!isdigit(oidstr[start])) {
			errno = EINVAL;
			goto failed;
		}

		if (order < 2) {
			/* Skip the first two components. */
			order++;
			continue;
		}

		errno = 0;
		oval = strtoull(&oidstr[start], &endp, 10);
		if (errno != 0 || endp != &oidstr[nsep]) {
			/*  overflow and invalid sequences. */
			if (*endp != '.')
				errno = EINVAL;
			goto failed;
		}

		if (!arc_encode(oidf, oval))
			goto failed;
	}

	if (ferror(oidf))
		goto failed;

	fclose(oidf);
	*outsz = oidsz;
	return ((uint8_t *)buf);

failed:
	serrno = errno;
	fclose(oidf);
	free(buf);
	errno = serrno;
	return (NULL);
}

static bool
arc_decode_next(const uint8_t **oidp, size_t *oidszp, uint64_t *valp)
{
	const uint8_t *oid = *oidp;
	size_t oidsz = *oidszp;
	uint64_t val = 0;
	uint8_t obyte;

	do {
		if (oidsz == 0)
			return (false);

		obyte = *oid;
		oid++;
		oidsz--;

		val <<= 7;
		val |= obyte & 0x7f;
	} while ((obyte & 0x80) != 0);

	*oidp = oid;
	*oidszp = oidsz;
	*valp = val;
	return (true);
}

char *
libder_oid_stringify(const uint8_t *oid, size_t oidsz)
{
	FILE *oidstrf;
	char *buf;
	size_t bufsz;
	uint64_t val;
	int serrno;
	bool rooted = false;

	oidstrf = open_memstream(&buf, &bufsz);
	if (oidstrf == NULL)
		return (NULL);

	while (oidsz != 0) {
		if (!arc_decode_next(&oid, &oidsz, &val))
			goto failed;

		if (!rooted) {
			unsigned long long root, sec;

			/* 2 is unbounded, 0-1 occupy no more than 0-39. */
			root = MIN(val / 40, 2);
			sec = val - (root * 40);
			rooted = true;

			fprintf(oidstrf, "%llu.%llu", root, sec);
			continue;
		}

		fprintf(oidstrf, ".%llu", (unsigned long long)val);
	}

	if (ferror(oidstrf))
		goto failed;

	fclose(oidstrf);
	return (buf);
failed:
	serrno = errno;
	fclose(oidstrf);
	free(buf);
	errno = serrno;
	return (NULL);
}
