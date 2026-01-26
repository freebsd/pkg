/*-
 * Copyright (c) 2024 Keve Müller <kevemueller@users.github.com>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#if __has_include(<sys/endian.h>)
#include <sys/endian.h>
#elif __has_include(<endian.h>)
#include <endian.h>
#elif __has_include(<machine/endian.h>)
#include <machine/endian.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bsd_compat.h>
#include "private/binfmt_macho.h"

/**
 * Minimal Mach-O binary file parser for both FAT as well as plain binaries with
 * sufficient functionality to handle architecture, OS, file type, library
 * dependencies.
 * As well as utility functions to convert data into different formats.
 */

/**** Readers ****/

static ssize_t
read_fully(const int fd, const size_t len, void *dest)
{
	unsigned char *p = dest;
	size_t n = len;
	ssize_t x;
	while (n > 0) {
		if ((x = read(fd, p, n)) < 0) {
			if ( EAGAIN == errno) {
				continue;
			}
			return x;
		}
		if ( 0 == x) {
			return -1;
		}
		n -= x;
		p += x;
	}
	return len;
}

ssize_t
read_u32(const int fd, const bool swap, uint32_t *dest)
{
	unsigned char buf[4];
	ssize_t x;
	if ((x = read_fully(fd, sizeof(buf), buf)) < 0) {
		return x;
	}
	if (swap) {
		*dest = le32dec(buf);
	} else {
		*dest = be32dec(buf);
	}
	return x;
}

static ssize_t
read_u64(const int fd, const bool swap, uint64_t *dest)
{
	unsigned char buf[8];
	ssize_t x;
	if ((x = read_fully(fd, sizeof(buf), buf)) < 0) {
		return x;
	}
	if (swap) {
		*dest = le64dec(buf);
	} else {
		*dest = be64dec(buf);
	}
	return x;
}

static ssize_t
read_cpu_type(const int fd, const bool swap, cpu_type_subtype_t *dest)
{
	ssize_t n = 0, x;
	uint32_t cputype;
	uint32_t cpusubtype;

	READ(u32, cputype);
	READ(u32, cpusubtype);
	dest->type = cputype & ~CPU_ARCH_MASK;
	dest->type_is64 = (cputype & CPU_ARCH_MASK) == CPU_ARCH_ABI64;
	dest->type_is64_32 = (cputype & CPU_ARCH_MASK) == CPU_ARCH_ABI64_32;
	dest->subtype_islib64 = (cpusubtype & CPU_SUBTYPE_MASK) ==
	    CPU_SUBTYPE_LIB64;
	switch (dest->type) {
	case CPU_TYPE_ARM:
		dest->subtype_arm = cpusubtype & ~CPU_SUBTYPE_MASK;
		break;
	case CPU_TYPE_X86:
		dest->subtype_x86 = cpusubtype & ~CPU_SUBTYPE_MASK;
		break;
	case CPU_TYPE_POWERPC:
		dest->subtype_ppc = cpusubtype & ~CPU_SUBTYPE_MASK;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	return n;
}

static ssize_t
read_fat_arch(const int fd, const uint32_t magic, fat_arch_t *dest)
{
	ssize_t n = 0, x;
	const bool swap = magic == FAT_CIGAM || magic == FAT_CIGAM_64;

	READ(cpu_type, dest->cpu);
	uint32_t align;
	uint32_t reserved;

	switch (magic) {
	case FAT_MAGIC:
	case FAT_CIGAM:;
		uint32_t offset32;
		uint32_t size32;
		READ(u32, offset32);
		READ(u32, size32);
		READ(u32, align); // bits

		dest->offset = offset32;
		dest->size = size32;
		dest->align = align;
		break;
	case FAT_MAGIC_64:
	case FAT_CIGAM_64:
		READ(u64, dest->offset);
		READ(u64, dest->size);
		READ(u32, align);
		READ(u32, reserved);
		dest->align = align;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	return n;
}

static ssize_t
read_version(const int fd, const bool swap, macho_version_t *dest)
{
	ssize_t n = 0, x;

	uint32_t version;
	READ(u32, version);
	dest->major = (version >> 16) & 0xffff;
	dest->minor = (version >> 8) & 0xff;
	dest->patch = version & 0xff;
	return n;
}

ssize_t
read_min_version(const int fd, const bool swap, const uint32_t loadcmd,
    build_version_t **dest)
{
	ssize_t n = 0, x;

	*dest = malloc(sizeof(build_version_t));
	(*dest)->ntools = 0;
	switch (loadcmd) {
	case LC_VERSION_MIN_IPHONEOS:
		(*dest)->platform = PLATFORM_IOS;
		break;
	case LC_VERSION_MIN_MACOSX:
		(*dest)->platform = PLATFORM_MACOS;
		break;
	case LC_VERSION_MIN_TVOS:
		(*dest)->platform = PLATFORM_TVOS;
		break;
	case LC_VERSION_MIN_WATCHOS:
		(*dest)->platform = PLATFORM_WATCHOS;
		break;
	default:
		return -1;
	}
	READ(version, (*dest)->minos);
	READ(version, (*dest)->sdk);
	return n;
}

ssize_t
read_path(const int fd, const bool swap, const uint32_t loadcmdsize,
    char **dest)
{
	ssize_t n = 0, x;

	uint32_t name_ofs;
	READ(u32, name_ofs);
	if (-1 == (x = lseek(fd, name_ofs - 12, SEEK_CUR))) {
		return x;
	}
	n += name_ofs - 12;
	*dest = malloc(loadcmdsize - name_ofs + 1);
	if ((x = read_fully(fd, loadcmdsize - name_ofs, *dest)) < 0) {
		free(*dest);
		*dest = 0;
		return x;
	}
	n += x;
	(*dest)[loadcmdsize - name_ofs] = '\0';
	return n;
}

ssize_t
read_dylib(const int fd, const bool swap, const uint32_t loadcmdsize,
    dylib_t **dest)
{
	ssize_t n = 0, x;

	uint32_t name_ofs;
	uint32_t timestamp;
	macho_version_t current_version;
	macho_version_t compatibility_version;

	READ(u32, name_ofs);
	READ(u32, timestamp);
	READ(version, current_version);
	READ(version, compatibility_version);

	if (-1 == (x = lseek(fd, name_ofs - 24, SEEK_CUR))) {
		return x;
	}
	n += name_ofs - 24;

	*dest = malloc(sizeof(dylib_t) + loadcmdsize - name_ofs + 1);
	(*dest)->timestamp = timestamp;
	(*dest)->current_version = current_version;
	(*dest)->compatibility_version = compatibility_version;
	if ((x = read_fully(fd, loadcmdsize - name_ofs, (*dest)->path)) < 0) {
		free(*dest);
		*dest = 0;
		return x;
	}
	n += x;
	(*dest)->path[loadcmdsize - name_ofs] = '\0';
	return n;
}

ssize_t
read_build_version(const int fd, const bool swap, build_version_t **dest)
{
	ssize_t n = 0, x;

	uint32_t platform;
	macho_version_t minos;
	macho_version_t sdk;
	uint32_t ntools;

	READ(u32, platform);
	READ(version, minos);
	READ(version, sdk);
	READ(u32, ntools);

	*dest = malloc(
	    sizeof(build_version_t) + ntools * sizeof(tool_version_t));
	(*dest)->platform = platform;
	(*dest)->minos = minos;
	(*dest)->sdk = sdk;
	(*dest)->ntools = ntools;
	tool_version_t *p = (*dest)->tools;

	for (; ntools-- > 0; p++) {
		uint32_t tool;
		READ(u32, tool);
		p->tool = tool;
		READ(version, p->version);
	}
	return n;
}

ssize_t
read_macho_header(const int fd, macho_header_t *dest)
{
	ssize_t n = 0, x;
	uint32_t reserved;

	if ((x = read_u32(fd, false, &dest->magic) < 0)) {
		return x;
	}
	n += x;

	const bool swap = dest->swap = dest->magic == MH_CIGAM ||
	    dest->magic == MH_CIGAM_64;

	READ(cpu_type, dest->cpu);
	READ(u32, dest->filetype);
	READ(u32, dest->ncmds);
	READ(u32, dest->sizeofcmds);
	READ(u32, dest->flags);
	switch (dest->magic) {
	case MH_MAGIC_64:
	case MH_CIGAM_64:
		READ(u32, reserved);
		break;
	default:
		break;
	}
	return n;
}

ssize_t
read_macho_file(const int fd, macho_file_t **dest)
{
	ssize_t n = 0, x;

	uint32_t magic;
	if ((x = read_u32(fd, false, &magic)) < 0) {
		return x;
	}
	n += x;

	const bool swap = magic == FAT_CIGAM || magic == FAT_CIGAM_64 ||
	    magic == MH_CIGAM || magic == MH_CIGAM_64;

	uint32_t nfat_arch;
	fat_arch_t *p;
	switch (magic) {
	case FAT_MAGIC:
	case FAT_MAGIC_64:
	case FAT_CIGAM:
	case FAT_CIGAM_64:
		READ(u32, nfat_arch);
		*dest = malloc(
		    sizeof(macho_file_t) + nfat_arch * sizeof(fat_arch_t));
		(*dest)->magic = magic;
		(*dest)->narch = nfat_arch;
		p = (*dest)->arch;

		while (nfat_arch-- > 0) {
			if ((x = read_fat_arch(fd, magic, p)) < 0) {
				free(*dest);
				*dest = 0;
				return x;
			}
			n += x;
			p++;
		}
		break;

	case MH_MAGIC:
	case MH_MAGIC_64:
	case MH_CIGAM:
	case MH_CIGAM_64:
		nfat_arch = 1;
		*dest = malloc(
		    sizeof(macho_file_t) + nfat_arch * sizeof(fat_arch_t));
		(*dest)->magic = magic;
		(*dest)->narch = nfat_arch;
		p = (*dest)->arch;
		READ(cpu_type, p->cpu);
		off_t xo;
		if (-1 == (xo = lseek(fd, 0, SEEK_END))) {
			free(*dest);
			*dest = 0;
			return xo;
		}
		p->offset = 0;
		p->size = xo;
		p->align = 0; // number of trailing zero bits in size;
		n = xo;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	return n;
}

/**** OS -> Kernel conversion ****/

static macho_version_t macos_to_darwin[][2] = {
	// macOS Sequoia
	{ { 15, 2, 0 }, { 24, 2, 0 } },
	{ { 15, 1, 0 }, { 24, 1, 0 } },
	{ { 15, 0, 0 }, { 24, 0, 0 } },
	// macOS Sonoma
	{ { 14, 6, 0 }, { 23, 6, 0 } },
	{ { 14, 5, 0 }, { 23, 4, 0 } },
	{ { 14, 4, 0 }, { 23, 5, 0 } },
	{ { 14, 3, 0 }, { 23, 3, 0 } },
	{ { 14, 2, 0 }, { 23, 2, 0 } },
	{ { 14, 1, 0 }, { 23, 1, 0 } },
	{ { 14, 0, 0 }, { 23, 0, 0 } },
	// macOS Ventura
	{ { 13, 5, 0 }, { 22, 6, 0 } },
	{ { 13, 4, 0 }, { 22, 5, 0 } },
	{ { 13, 3, 0 }, { 22, 4, 0 } },
	{ { 13, 2, 0 }, { 22, 3, 0 } },
	{ { 13, 1, 0 }, { 22, 2, 0 } },
	{ { 13, 0, 0 }, { 22, 1, 0 } },
	// macOS Monterey
	{ { 12, 5, 0 }, { 21, 6, 0 } },
	{ { 12, 4, 0 }, { 21, 5, 0 } },
	{ { 12, 3, 0 }, { 21, 4, 0 } },
	{ { 12, 2, 0 }, { 21, 3, 0 } },
	{ { 12, 1, 0 }, { 21, 2, 0 } },
	{ { 12, 0, 1 }, { 21, 1, 0 } },
	{ { 12, 0, 0 }, { 21, 0, 1 } },
	// macOS Big Sur
	{ { 11, 5, 0 }, { 20, 6, 0 } },
	{ { 11, 4, 0 }, { 20, 5, 0 } },
	{ { 11, 3, 0 }, { 20, 4, 0 } },
	{ { 11, 2, 0 }, { 20, 3, 0 } },
	{ { 11, 1, 0 }, { 20, 2, 0 } },
	{ { 11, 0, 0 }, { 20, 1, 0 } },
	// macOS Catalina
	{ { 10, 15, 6 }, { 19, 6, 0 } },
	{ { 10, 15, 5 }, { 19, 5, 0 } },
	{ { 10, 15, 4 }, { 19, 4, 0 } },
	{ { 10, 15, 3 }, { 19, 3, 0 } },
	{ { 10, 15, 2 }, { 19, 2, 0 } },
	{ { 10, 15, 0 }, { 19, 0, 0 } },
	// macOS Mojave
	{ { 10, 14, 6 }, { 18, 7, 0 } },
	{ { 10, 14, 5 }, { 18, 6, 0 } },
	{ { 10, 14, 4 }, { 18, 5, 0 } },
	{ { 10, 14, 1 }, { 18, 2, 0 } },
	{ { 10, 14, 0 }, { 18, 0, 0 } },
	// macOS High Sierra
	{ { 10, 13, 6 }, { 17, 7, 0 } },
	{ { 10, 13, 5 }, { 17, 6, 0 } },
	{ { 10, 13, 4 }, { 17, 5, 0 } },
	{ { 10, 13, 3 }, { 17, 4, 0 } },
	{ { 10, 13, 2 }, { 17, 3, 0 } },
	{ { 10, 13, 1 }, { 17, 2, 0 } },
	{ { 10, 13, 0 }, { 17, 0, 0 } },
	// macOS Sierra
	{ { 10, 12, 6 }, { 16, 7, 0 } },
	{ { 10, 12, 5 }, { 16, 6, 0 } },
	{ { 10, 12, 4 }, { 16, 5, 0 } },
	{ { 10, 12, 3 }, { 16, 4, 0 } },
	{ { 10, 12, 2 }, { 16, 3, 0 } },
	{ { 10, 12, 1 }, { 16, 1, 0 } },
	{ { 10, 12, 0 }, { 16, 0, 0 } },
	// OS X El Capitan
	{ { 10, 11, 6 }, { 15, 6, 0 } },
	{ { 10, 11, 5 }, { 15, 5, 0 } },
	{ { 10, 11, 4 }, { 15, 4, 0 } },
	{ { 10, 11, 3 }, { 15, 3, 0 } },
	{ { 10, 11, 2 }, { 15, 2, 0 } },
	{ { 10, 11, 0 }, { 15, 0, 0 } },
	// OS X Yosemite
	{ { 10, 10, 5 }, { 14, 5, 0 } },
	{ { 10, 10, 4 }, { 14, 4, 0 } },
	{ { 10, 10, 3 }, { 14, 3, 0 } },
	{ { 10, 10, 2 }, { 14, 1, 0 } },
	{ { 10, 10, 0 }, { 14, 0, 0 } },
	// OS X Mavericks
	{ { 10, 9, 5 }, { 13, 4, 0 } },
	{ { 10, 9, 4 }, { 13, 3, 0 } },
	{ { 10, 9, 3 }, { 13, 2, 0 } },
	{ { 10, 9, 2 }, { 13, 1, 0 } },
	{ { 10, 9, 0 }, { 13, 0, 0 } },
	// OS X Mountain Lion
	{ { 10, 8, 5 }, { 12, 5, 0 } }, // Build 12F45 switched to 12.6
	{ { 10, 8, 4 }, { 12, 4, 0 } },
	{ { 10, 8, 3 }, { 12, 3, 0 } },
	{ { 10, 8, 2 }, { 12, 2, 0 } },
	{ { 10, 8, 1 }, { 12, 1, 0 } },
	{ { 10, 8, 0 }, { 12, 0, 0 } },
	// OS X Lion
	{ { 10, 7, 5 }, { 11, 4, 2 } },
	{ { 10, 7, 4 }, { 11, 4, 0 } },
	{ { 10, 7, 3 }, { 11, 3, 0 } },
	{ { 10, 7, 2 }, { 11, 2, 0 } },
	{ { 10, 7, 1 }, { 11, 1, 0 } },
	{ { 10, 7, 0 }, { 11, 0, 0 } },
	// Mac OS X Snow Leopard
	{ { 10, 6, 8 }, { 10, 8, 0 } },
	{ { 10, 6, 7 }, { 10, 7, 0 } },
	{ { 10, 6, 6 }, { 10, 6, 0 } },
	{ { 10, 6, 5 }, { 10, 5, 0 } },
	{ { 10, 6, 4 }, { 10, 4, 0 } },
	{ { 10, 6, 3 }, { 10, 3, 0 } },
	{ { 10, 6, 2 }, { 10, 2, 0 } },
	{ { 10, 6, 1 }, { 10, 1, 0 } },
	{ { 10, 6, 0 }, { 10, 0, 0 } },
	// Mac OS X Leopard
	{ { 10, 5, 8 }, { 9, 8, 0 } },
	{ { 10, 5, 7 }, { 9, 7, 0 } },
	{ { 10, 5, 6 }, { 9, 6, 0 } },
	{ { 10, 5, 5 }, { 9, 5, 0 } },
	{ { 10, 5, 4 }, { 9, 4, 0 } },
	{ { 10, 5, 3 }, { 9, 3, 0 } },
	{ { 10, 5, 2 }, { 9, 2, 0 } },
	{ { 10, 5, 1 }, { 9, 1, 0 } }, // Build 9B2117 switched to 9.1.1
	{ { 10, 5, 0 }, { 9, 0, 0 } },
	// Mac OS X Tiger
	{ { 10, 4, 11 }, { 8, 11, 0 } },
	{ { 10, 4, 10 }, { 8, 10, 0 } },
	{ { 10, 4, 9 }, { 8, 9, 0 } },
	{ { 10, 4, 8 }, { 8, 8, 0 } },
	{ { 10, 4, 7 }, { 8, 7, 0 } },
	{ { 10, 4, 6 }, { 8, 6, 0 } },
	{ { 10, 4, 5 }, { 8, 5, 0 } },
	{ { 10, 4, 4 }, { 8, 4, 0 } },
	{ { 10, 4, 3 }, { 8, 3, 0 } },
	{ { 10, 4, 2 }, { 8, 2, 0 } },
	{ { 10, 4, 1 }, { 8, 1, 0 } },
	{ { 10, 4, 0 }, { 8, 0, 0 } },
	// Mac OS X Panther
	{ { 10, 3, 9 }, { 7, 9, 0 } },
	{ { 10, 3, 8 }, { 7, 8, 0 } },
	{ { 10, 3, 7 }, { 7, 7, 0 } },
	{ { 10, 3, 6 }, { 7, 6, 0 } },
	{ { 10, 3, 5 }, { 7, 5, 0 } },
	{ { 10, 3, 4 }, { 7, 4, 0 } },
	{ { 10, 3, 3 }, { 7, 3, 0 } },
	{ { 10, 3, 2 }, { 7, 2, 0 } },
	{ { 10, 3, 1 }, { 7, 1, 0 } },
	{ { 10, 3, 0 }, { 7, 0, 0 } },
	// Mac OS X Jaguar
	{ { 10, 2, 8 }, { 6, 8, 0 } },
	{ { 10, 2, 7 }, { 6, 7, 0 } },
	{ { 10, 2, 6 }, { 6, 6, 0 } },
	{ { 10, 2, 5 }, { 6, 5, 0 } },
	{ { 10, 2, 4 }, { 6, 4, 0 } },
	{ { 10, 2, 3 }, { 6, 3, 0 } },
	{ { 10, 2, 2 }, { 6, 2, 0 } },
	{ { 10, 2, 1 }, { 6, 1, 0 } },
	{ { 10, 2, 0 }, { 6, 0, 0 } },
	// Mac OS X 10.1 Puma
	{ { 10, 1, 5 }, { 5, 5, 0 } },
	{ { 10, 1, 4 }, { 5, 4, 0 } },
	{ { 10, 1, 3 }, { 5, 3, 0 } },
	{ { 10, 1, 2 }, { 5, 2, 0 } },
	{ { 10, 1, 1 }, { 5, 1, 0 } },
	{ { 10, 1, 0 }, { 1, 4, 1 } },
	// Mac OS X 10.0 Cheetah
	{ { 10, 0, 1 }, { 1, 3, 1 } },
	{ { 10, 0, 0 }, { 1, 3, 0 } },
	// Mac OS X Public Beta
	// {{x,y,z}}, {1,2,1}},
	// Mac OS X Server 1.0
	{ { 1, 0, 2 }, { 0, 3, 0 } },
	{ { 1, 0, 1 }, { 0, 2, 0 } },
	{ { 1, 0, 0 }, { 0, 1, 0 } },
	// EOA
	{ { 0, 0, 0 }, { 0, 0, 0 } },
};

static macho_version_t ios_to_darwin[][2] = {
	// iOS 18, iPadOS 18, tvOS 18
	{ { 18, 0, 0 }, { 24, 0, 0 } },
	// iOS 17, iPadOS 17, tvOS 17
	{ { 17, 5, 0 }, { 23, 5, 0 } },
	{ { 17, 4, 0 }, { 23, 4, 0 } },
	{ { 17, 3, 0 }, { 23, 3, 0 } },
	{ { 17, 2, 0 }, { 23, 2, 0 } },
	{ { 17, 1, 0 }, { 23, 1, 0 } },
	{ { 17, 0, 0 }, { 23, 0, 0 } },
	// iOS 16, iPadOS 16, tvOS 16
	{ { 16, 6, 0 }, { 22, 6, 0 } },
	{ { 16, 5, 0 }, { 22, 5, 0 } },
	{ { 16, 4, 0 }, { 22, 4, 0 } },
	{ { 16, 3, 0 }, { 22, 3, 0 } },
	{ { 16, 2, 0 }, { 22, 2, 0 } },
	{ { 16, 1, 0 }, { 22, 1, 0 } },
	{ { 16, 0, 0 }, { 22, 0, 0 } },
	// iOS 15, iPadOS 15, tvOS 15
	{ { 15, 6, 0 }, { 21, 6, 0 } },
	{ { 15, 5, 0 }, { 21, 5, 0 } },
	{ { 15, 4, 0 }, { 21, 4, 0 } },
	{ { 15, 3, 0 }, { 21, 3, 0 } },
	{ { 15, 2, 0 }, { 21, 2, 0 } },
	{ { 15, 0, 0 }, { 21, 1, 0 } },
	// iOS 15.0 beta 1 -> 21.0.0
	// iOS 14, iPadOS 14, tvOS 14
	{ { 14, 7, 0 }, { 20, 6, 0 } },
	{ { 14, 6, 0 }, { 20, 5, 0 } },
	{ { 14, 5, 0 }, { 20, 4, 0 } },
	{ { 14, 4, 0 }, { 20, 3, 0 } },
	{ { 14, 3, 0 }, { 20, 2, 0 } },
	{ { 14, 0, 0 }, { 20, 0, 0 } },
	// iOS 13
	{ { 13, 6, 0 }, { 19, 6, 0 } },
	{ { 13, 5, 0 }, { 19, 5, 0 } },
	{ { 13, 3, 1 }, { 19, 3, 0 } },
	{ { 13, 3, 0 }, { 19, 2, 0 } },
	// iOS 12
	{ { 12, 1, 0 }, { 18, 2, 0 } },
	// iOS 11
	{ { 11, 4, 1 }, { 17, 7, 0 } },
	// iOS 10
	{ { 10, 3, 3 }, { 16, 6, 0 } },
	{ { 10, 3, 0 }, { 16, 3, 0 } },
	{ { 10, 0, 1 }, { 16, 0, 0 } },
	// iOS 9
	{ { 9, 3, 3 }, { 15, 6, 0 } },
	{ { 9, 0, 0 }, { 15, 0, 0 } },
	// iOS 7, iOS 8
	{ { 7, 0, 0 }, { 14, 0, 0 } },
	// iOS 6
	{ { 6, 0, 0 }, { 13, 0, 0 } },
	// iOS 4.3
	{ { 4, 3, 0 }, { 11, 0, 0 } },
	// iPhone OS 3
	{ { 3, 0, 0 }, { 10, 0, 0 } },
	// iPhone OS 1
	{ { 1, 0, 0 }, { 9, 0, 0 } },
	// EOA
	{ { 0, 0, 0 }, { 0, 0, 0 } },
};

int
map_platform_to_darwin(macho_version_t *darwin,
    const enum MachoPlatform platform, const macho_version_t version)
{
	macho_version_t *p;
	switch (platform) {
	case PLATFORM_MACOS:
		p = macos_to_darwin[0];
		break;

	case PLATFORM_IOS:
	case PLATFORM_IOSSIMULATOR:
	case PLATFORM_TVOS:
	case PLATFORM_TVOSSIMULATOR:
		p = ios_to_darwin[0];
		break;

	case PLATFORM_WATCHOS:
	case PLATFORM_WATCHOSSIMULATOR:
		darwin->major = version.major + 13;
		darwin->minor = version.minor;
		darwin->patch = 0;
		return 0;

	default:
		return -1;
	}
	while (p->major > version.major || p->minor > version.minor ||
	    p->patch > version.patch) {
		p += 2;
	}
	p++;
	if (0 == p->major && 0 == p->minor && 0 == p->patch) {
		return -1;
	}
	*darwin = *p;
	return 0;
}
