/*-
 * Copyright (c) 2024 Keve Müller <kevemueller@users.github.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <errno.h>

#include "private/binfmt_macho.h"
#include "private/pkg.h"
#include "private/event.h"

/**
 * Routines to support pkg_abi.c functions when dealing with Mach-O files.
 * Supports getting ABI and ALTABI from the binary's load commands. Cave: picks
 * first binary in FAT collection. Supports getting shared libary information.
 * Picks right binary in FAT collection based on ABI. Supports FreeBSD naming of
 * architectures.
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

static cpu_type_subtype_t
freebsd_machine_arch_to_cputype(const char *archname) {
	cpu_type_subtype_t cpu = { 0 };

	if (!strcmp("aarch64", archname)) {
		cpu.type = CPU_TYPE_ARM;
		cpu.type_is64 = true;
	} else if (!strcmp("amd64", archname)) {
		cpu.type = CPU_TYPE_X86;
		cpu.type_is64 = true;
		cpu.subtype_x86 = CPU_SUBTYPE_X86_ALL;
	} else if (!strcmp("arm", archname)) {
		cpu.type = CPU_TYPE_ARM;
		cpu.subtype_arm = CPU_SUBTYPE_ARM_ALL;
	} else if (!strcmp("armeb", archname)) {
		cpu.type = CPU_TYPE_ARM;
		cpu.subtype_arm = CPU_SUBTYPE_ARM_V5;
	} else if (!strcmp("armv6", archname)) {
		cpu.type = CPU_TYPE_ARM;
		cpu.subtype_arm = CPU_SUBTYPE_ARM_V6;
	} else if (!strcmp("armv7", archname)) {
		cpu.type = CPU_TYPE_ARM;
		cpu.subtype_arm = CPU_SUBTYPE_ARM_V7;
	} else if (!strcmp("i386", archname)) {
		cpu.type = CPU_TYPE_X86;
		cpu.subtype_x86 = CPU_SUBTYPE_X86_ALL;
	} else if (!strcmp("powerpc", archname)) {
		cpu.type = CPU_TYPE_POWERPC;
		cpu.subtype_ppc = CPU_SUBTYPE_POWERPC_ALL;
	} else if (!strcmp("powerpc64", archname)) {
		cpu.type = CPU_TYPE_POWERPC;
		cpu.type_is64 = true;
		cpu.subtype_ppc = CPU_SUBTYPE_POWERPC_ALL;
	} else {
		// alpha
		// ia64
		// mips*
		// pc98
		// sparc64
		cpu.type = CPU_TYPE_ANY;
	}
	return cpu;
}


/**
 * Using the passed mf descriptor, match the best entry using oi->name as a hint.
 * No hint or no architecture in hint -> first entry. Debug1 warning if this is not precise match (there were multiple to choose from)
 * Hint -> always match, even if single architecture in file. Notice if match fails and return null.
 */
static const fat_arch_t *
match_entry(macho_file_t *mf, struct os_info *oi)
{
	const fat_arch_t *p = mf->arch;
	// we can change the content of oi->abi freely
	char *abisep = oi->abi;
	/*const char *osname = */strsep(&abisep, ":");
	/*const char *version_str = */ strsep(&abisep, ":");
	const char *archname = strsep(&abisep, ":");
	if (archname) {	
		const cpu_type_subtype_t cpu_hint = freebsd_machine_arch_to_cputype(archname);
		const fat_arch_t *p_end = p + mf->narch;
		while (p < p_end) {
			// do not match cpu_hint.type == CPU_TYPE_ANY which is used if the 
			// archname hint was not recognized
			if (p->cpu.type == cpu_hint.type &&
			    p->cpu.type_is64 == cpu_hint.type_is64) {
				switch (cpu_hint.type) {
				case CPU_TYPE_ARM:
					if (p->cpu.subtype_arm ==
						CPU_SUBTYPE_ARM_ALL ||
					    cpu_hint.subtype_arm ==
						CPU_SUBTYPE_ARM_ALL ||
					    p->cpu.subtype_arm ==
						cpu_hint.subtype_arm) {
						return p;
					}
					break;
				case CPU_TYPE_POWERPC:
					if (p->cpu.subtype_ppc ==
						CPU_SUBTYPE_POWERPC_ALL ||
					    cpu_hint.subtype_ppc ==
						CPU_SUBTYPE_POWERPC_ALL ||
					    p->cpu.subtype_ppc ==
						cpu_hint.subtype_ppc) {
						return p;
					}
					break;
				case CPU_TYPE_X86:
					if (p->cpu.subtype_x86 ==
						CPU_SUBTYPE_X86_ALL ||
					    cpu_hint.subtype_x86 ==
						CPU_SUBTYPE_X86_ALL ||
					    p->cpu.subtype_x86 ==
						cpu_hint.subtype_x86) {
						return p;
					}
					break;
				default:
					break;
				}
			}
			pkg_debug(1, "Looking for %s, did not match %s",
		    archname, cputype_to_freebsd_machine_arch(p->cpu));
			p++;
		}
		pkg_emit_notice("Scanned %d entr%s, found none matching selector %s",
			mf->narch, mf->narch > 1 ? "ies" : "y", archname);
		return 0;	
	} else if (mf->narch > 1 ) {
		pkg_debug(1,"Found %d entries in universal binary, picking first",
			mf->narch);
	}
	return p;
}

/**
 * With a not-null, potentially pre-populated os_info structure, fill
 * all members of os_info except altabi with values obtained by parsing the Mach-O
 * file passed with file descriptor.
 *
 * Third (architecture) component of oi->abi is used to determine the fat entry to be parsed
 * in a universal binary. when not set, the first entry is used.
 *
 * Returns EPKG_OK if all went fine, EPKG_FATAL if anything went wrong.
 * Seeks the file descriptor to an arbitrary position.
 */
int
pkg_get_myarch_macho(int fd, struct os_info *oi)
{
	ssize_t x;
	pkg_error_t ret = EPKG_FATAL;

	macho_file_t *mf = 0;
	build_version_t *bv = 0;

	if ((x = read_macho_file(fd, &mf)) < 0) {
		goto cleanup;
	}

	const fat_arch_t *p = match_entry(mf, oi);

	if (!p) {
		goto cleanup;
	}

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
		enum MachOLoadCommand loadcmd = loadcmdtype & ~LC_REQ_DYLD;
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
				if ((x = read_min_version(fd, swap, loadcmd,
					 &bv)) < 0) {
					goto cleanup;
				}
				n += x;
				break;
			}
			// have seen the more precise
			// LC_BUILD_VERSION already
			// fall through and disregard this
		default:
			break;
		}
		const uint32_t fill = loadcmdsize - (n - n0);
		if (fill && -1 == (x = lseek(fd, fill, SEEK_CUR))) {
			goto cleanup;
		}
		n += fill;
		if (n > mh.sizeofcmds) {
			// we passed the frame boundary of the load commands
			pkg_emit_error("Mach-O structure misread.");
			errno = EINVAL;
			goto cleanup;
		}
	}

	if (bv) {
		macho_version_t darwin;
		map_platform_to_darwin(&darwin, bv->platform, bv->minos);

		oi->osversion = darwin.major * 100000 + darwin.minor * 1000 +
		    darwin.patch;
		oi->ostype = OS_MACOS;
		free(oi->name);
		oi->name = xstrdup("Darwin");
		free(oi->version);
		if (darwin.patch) {
			xasprintf(&oi->version, "%d.%d.%d", darwin.major,
			    darwin.minor, darwin.patch);
		} else {
			xasprintf(&oi->version, "%d.%d", darwin.major,
			    darwin.minor);
		}
		free(oi->version_major);
		xasprintf(&oi->version_major, "%d", darwin.major);
		free(oi->version_minor);
		xasprintf(&oi->version_minor, "%d", darwin.minor);
		free(oi->arch);
		oi->arch = xstrdup(cputype_to_freebsd_machine_arch(mh.cpu));
		snprintf(oi->abi, sizeof(oi->abi), "Darwin:%d:%s", darwin.major, cputype_to_freebsd_machine_arch(mh.cpu)); 
		// not populating oi->altabi, derived later by caller.
		snprintf(oi->str_osversion, sizeof(oi->str_osversion), "%d",
		    oi->osversion);

		ret = EPKG_OK;
	} else {
		pkg_emit_notice("No OS version information found in binary.");
		ret = EPKG_WARN;
	}

cleanup:
	free(bv);
	free(mf);
	return ret;
}

int
pkg_analyse_init_macho(__unused const char *stage)
{
	return EPKG_OK;
}

int
pkg_analyse_macho(const bool developer_mode, __unused struct pkg *pkg, __unused const char *fpath)
{
	int ret = EPKG_OK;
	// int ret = analyse_macho(pkg, fpath);
	if (developer_mode) {
		if (ret != EPKG_OK && ret != EPKG_END) {
			return EPKG_WARN;
		}
	}
	return ret;
}

int
pkg_analyse_close_macho()
{
	return EPKG_OK;
}
