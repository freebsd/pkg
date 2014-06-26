/* $FreeBSD$ */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#ifdef HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#endif

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif

/* 1234 = LIL_ENDIAN, 4321 = BIGENDIAN */
#if BYTE_ORDER == LITTLE_ENDIAN
#define BYTEORDER 1234
#else
#define BYTEORDER 4321
#endif

/* whether byteorder is bigendian */
#if BYTE_ORDER == BIG_ENDIAN
#define WORDS_BIGENDIAN
#else
#undef WORDS_BIGENDIAN 
#endif

/* Define to specify how much context to retain around the current parse
   point. */
#define XML_CONTEXT_BYTES 1024

/* Define to make parameter entity parsing functionality available. */
#define XML_DTD 1

/* Define to make XML Namespaces functionality available. */
#define XML_NS 1


