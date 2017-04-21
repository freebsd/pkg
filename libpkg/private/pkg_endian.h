/*-
 * Copyright (c) 2017 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#ifndef PKG_PKG_ENDIAN_H
#define PKG_PKG_ENDIAN_H

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/types.h>
#include <stdint.h>

#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#elif HAVE_ENDIAN_H
#include <endian.h>
#elif HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#endif

#ifndef BYTE_ORDER

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN   1234
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN      4321
#endif

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN || \
    defined(__BIG_ENDIAN__) || \
    defined(__ARMEB__) || \
    defined(__THUMBEB__) || \
    defined(__AARCH64EB__) || \
    defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__)
#define BYTE_ORDER BIG_ENDIAN
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN || \
    defined(__LITTLE_ENDIAN__) || \
    defined(__ARMEL__) || \
    defined(__THUMBEL__) || \
    defined(__AARCH64EL__) || \
    defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__)
#define BYTE_ORDER LITTLE_ENDIAN
#else
#error "I don't know what architecture this is!"
#endif

#define pkg_bswap16(x) (((x)<<8)|((x)>>8))
#if defined(_MSC_VER)
#define pkg_bswap32(x) _byteswap_uint32_t(x)
#define pkg_bswap64(x) _byteswap_uint64_t(x)
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define pkg_bswap32(x) OSSwapInt32(x)
#define pkg_bswap64(x) OSSwapInt64(x)
#elif defined(__GNUC__)
#define pkg_bswap32(x) __builtin_bswap32(x)
#define pkg_bswap64(x) __builtin_bswap64(x)
#else
#define pkg_bswap32(x) bswap32(x)
#define pkg_bswap64(x) bswap64(x)
#endif

#endif /* BYTE_ORDER */


static inline uint64_t
pkg_le64(uint64_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return v;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return pkg_bswap64(v);
#else
#error "Unknown endianess"
#endif
}

static inline uint32_t
pkg_le32(uint32_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return v;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return pkg_bswap32(v);
#else
#error "Unknown endianess"
#endif
}

static inline uint16_t
pkg_le16(uint16_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return v;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return pkg_bswap16(v);
#else
#error "Unknown endianess"
#endif
}

#endif
