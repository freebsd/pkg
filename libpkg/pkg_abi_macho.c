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

#include <errno.h>

#include "private/binfmt_macho.h"

#include "private/pkg.h"
#include "private/event.h"


/**
 * Routines to support pkg_abi.c functions when dealing with Mach-O files.
 * Supports getting ABI and ALTABI from the binary's load commands. Cave: picks first binary in FAT collection.
 * Supports getting shared libary information. Picks right binary in FAT collection based on ABI.
 * Supports FreeBSD naming of architectures.
 */

/**** CPU -> FreeBSD MACHINE_ARCH conversion ****/

static const char *
cputype_to_freebsd_machine_arch(const cpu_type_subtype_t cpu)
{
	switch (cpu.type) {
	case CPU_TYPE_ARM:
		if (cpu.type_is64_32) {
			return "aarch64-x32";
		} else if (cpu.type_is64) {
			return "aarch64";
		} else {
			switch (cpu.subtype_arm) {
			case CPU_SUBTYPE_ARM_V7:
			case CPU_SUBTYPE_ARM_V7S:
			case CPU_SUBTYPE_ARM_V7K:
			case CPU_SUBTYPE_ARM_V7M:
			case CPU_SUBTYPE_ARM_V7EM:
				return "armv7";
			case CPU_SUBTYPE_ARM_V6:
			case CPU_SUBTYPE_ARM_V6M:
				return "armv6";
			case CPU_SUBTYPE_ARM_XSCALE:
			case CPU_SUBTYPE_ARM_V5:
			case CPU_SUBTYPE_ARM_V4T:
				return "armeb";
			case CPU_SUBTYPE_ARM_ALL:
			default:
				return "arm";
			}
		}
	case CPU_TYPE_POWERPC:
		if (cpu.type_is64_32) {
			return "powerpc64-x32";
		} else if (cpu.type_is64) {
			return "powerpc64";
		} else {
			return "powerpc";
		}
	case CPU_TYPE_X86:
		if (cpu.type_is64_32) {
			return "amd64-x32";
		} else if (cpu.type_is64) {
			return "amd64";
		} else {
			return "i386";
		}
	default:
		return "unknown";
	}
}

int
pkg_get_myarch_macho(int fd, struct os_info *oi)
{
	ssize_t x;
	char *dest = oi->abi;
	size_t sz = sizeof(oi->abi);

	macho_file_t *mf = 0;
	build_version_t *bv = 0;

	if ((x = read_macho_file(fd, &mf)) < 0) {
		goto cleanup;
	}

	if (0 == mf->narch) {
		goto cleanup;
	}
	if (mf->narch > 1) {
		pkg_debug(1, "Found %d entries, picking first", mf->narch);
	}
	fat_arch_t *p = mf->arch;

	if (-1 == (x = lseek(fd, p->offset, SEEK_SET))) {
		goto cleanup;
	}
	size_t n = 0;
	macho_header_t mh;
	if ((x = read_macho_header(fd, &mh)) < 0) {
		goto cleanup;
	}
	const bool swap = mh.swap;
	n = 0;
	for (uint32_t ui = mh.ncmds; ui-- > 0;) {
			size_t n0 = n;
			uint32_t loadcmdtype;
			uint32_t loadcmdsize;
			READ(u32, loadcmdtype);
			READ(u32, loadcmdsize);
			enum MachOLoadCommand loadcmd = loadcmdtype &
			    ~LC_REQ_DYLD;
			switch (loadcmd) {
			case LC_BUILD_VERSION:
				if (bv) { // overwrite previous LC_VERSION_MIN_X
					  // values
					free(bv);
					bv = 0;
				}
				READ(build_version, bv);
				break;
			case LC_VERSION_MIN_IPHONEOS:
			case LC_VERSION_MIN_MACOSX:
			case LC_VERSION_MIN_TVOS:
			case LC_VERSION_MIN_WATCHOS:
				if (!bv) {
					if ((x = read_min_version(fd, swap,
						 loadcmd, &bv)) < 0) {
						goto cleanup;
					}
					n += x;
					break;
				}
				// have seen the more precise
				// LC_BUILD_VERSION already
				// fall through and disregard this
			default:
				if (-1 ==
				    (x = lseek(fd, loadcmdsize - 8,
					 SEEK_CUR))) {
					goto cleanup;
				}
				n += loadcmdsize - 8;
				break;
			}
			if (n - n0 != loadcmdsize) {
				printf("unprecise read %u != %zu", n - n0,
				    loadcmdsize);
				errno = EINVAL;
				goto cleanup;
			}
			if (n > mh.sizeofcmds) {
				printf("long read %u > %u", n, mh.sizeofcmds);
				errno = EINVAL;
				goto cleanup;
			}
	}

	if (bv) {
		macho_version_t darwin;
		map_platform_to_darwin(&darwin, bv->platform, bv->minos);
		snprintf(dest, sz, "Darwin:%d:%s", darwin.major, cputype_to_freebsd_machine_arch(mh.cpu));
		if (oi) {
			oi->name = xstrdup("Darwin");
			oi->osversion = darwin.major * 100000 + darwin.minor * 1000 + darwin.patch;
			if (darwin.patch) {
				xasprintf(&oi->version, "%d.%d.%d", darwin.major, darwin.minor, darwin.patch);
			} else {
				xasprintf(&oi->version, "%d.%d", darwin.major, darwin.minor);
			}
			xasprintf(&oi->version_major, "%d", darwin.major);
			xasprintf(&oi->version_minor, "%d", darwin.minor);
			oi->arch = xstrdup(cputype_to_freebsd_machine_arch(
				mh.cpu));
		}
		return EPKG_OK;
	}


cleanup:
	if (bv) {
		free(bv);
	}
	if (mf) {
		free(mf);
	}
	return EPKG_FATAL;
}


int pkg_analyse_init_macho(__unused const char* stage) {
	return EPKG_OK;
}

int pkg_analyse_macho(const bool developer_mode, struct pkg *pkg, const char *fpath) {
	int ret = EPKG_OK;
	// int ret = analyse_macho(pkg, fpath);
	if (developer_mode) {
		if (ret != EPKG_OK && ret != EPKG_END) {
			return EPKG_WARN;
		}
	}
	return ret;
}

int pkg_analyse_close_macho() {
	return EPKG_OK;
}
