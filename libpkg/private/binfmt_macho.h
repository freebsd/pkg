/*-
 * Copyright (c) 2024 Keve Müller <kevemueller@users.github.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _PKG_BINFMT_MACHO_H
#define _PKG_BINFMT_MACHO_H

#include <sys/types.h>

#include <stdbool.h>
#include <stdint.h>

/**** Magic numbers & constants ****/

// Constants for magic (big&little endian)
#define MH_MAGIC     0xFEEDFACEu
#define MH_CIGAM     0xCEFAEDFEu
#define MH_MAGIC_64  0xFEEDFACFu
#define MH_CIGAM_64  0xCFFAEDFEu
#define FAT_MAGIC    0xCAFEBABEu
#define FAT_CIGAM    0xBEBAFECAu
#define FAT_MAGIC_64 0xCAFEBABFu
#define FAT_CIGAM_64 0xBFBAFECAu

// Masks for CPUType capability bits
static const uint32_t CPU_ARCH_MASK = 0xff000000u;
static const uint32_t CPU_ARCH_ABI64 = 0x01000000u; // 64 bit ABI
static const uint32_t CPU_ARCH_ABI64_32 =
    0x02000000u; // ILP32 ABI on 64-bit hardware

// Masks for the CPUSubType
static const uint32_t CPU_SUBTYPE_MASK =
    0xff000000u; // Mask for architecture bits
static const uint32_t CPU_SUBTYPE_LIB64 = 0x80000000u; // 64 bit libraries
// static const uint32_t  CPU_SUBTYPE_MULTIPLE = ~0u;

// // arm64e uses the capability bits to encode ptrauth ABI information.
// // Bit 63 marks the binary as Versioned.
// CPU_SUBTYPE_ARM64E_VERSIONED_PTRAUTH_ABI_MASK = 0x80000000U,
// // Bit 62 marks the binary as using a kernel ABI.
// CPU_SUBTYPE_ARM64E_KERNEL_PTRAUTH_ABI_MASK = 0x40000000U,
// // Bits [59:56] hold the 4-bit ptrauth ABI version.
// CPU_SUBTYPE_ARM64E_PTRAUTH_MASK = 0x0f000000U,

enum CPUType {
	CPU_TYPE_ANY = -1,
	CPU_TYPE_VAX = 1,
	CPU_TYPE_ROMP,
	CPU_TYPE_NS32032 = 4,
	CPU_TYPE_NS32332,
	CPU_TYPE_MC680x0,
	CPU_TYPE_X86,
	CPU_TYPE_MIPS,
	CPU_TYPE_NS32352,
	CPU_TYPE_MC98000,
	CPU_TYPE_HPPA,
	CPU_TYPE_ARM,
	CPU_TYPE_MC88000,
	CPU_TYPE_SPARC,
	CPU_TYPE_I860BE,
	CPU_TYPE_I860LE,
	CPU_TYPE_RS6000,
	CPU_TYPE_POWERPC
};

enum CPUSubTypeX86 {
	CPU_SUBTYPE_X86_INVALID = -1,
	CPU_SUBTYPE_X86_ALL = 3,
	CPU_SUBTYPE_486 = 4,
	CPU_SUBTYPE_486SX = 0x84,
	CPU_SUBTYPE_586 = 5,
	CPU_SUBTYPE_PENTPRO = 0x16,
	CPU_SUBTYPE_PENTII_M3 = 0x36,
	CPU_SUBTYPE_PENTII_M5 = 0x56,
	CPU_SUBTYPE_CELERON = 0x67,
	CPU_SUBTYPE_CELERON_MOBILE = 0x77,
	CPU_SUBTYPE_PENTIUM_3 = 0x08,
	CPU_SUBTYPE_PENTIUM_3_M = 0x18,
	CPU_SUBTYPE_PENTIUM_3_XEON = 0x28,
	CPU_SUBTYPE_PENTIUM_M = 0x09,
	CPU_SUBTYPE_PENTIUM_4 = 0x0a,
	CPU_SUBTYPE_PENTIUM_4_M = 0x1a,
	CPU_SUBTYPE_ITANIUM = 0x0b,
	CPU_SUBTYPE_ITANIUM_2 = 0x1b,
	CPU_SUBTYPE_XEON = 0x0c,
	CPU_SUBTYPE_XEON_MP = 0x1c
};

enum CPUSubTypeARM {
	CPU_SUBTYPE_ARM_INVALID = -1,
	CPU_SUBTYPE_ARM_ALL,
	CPU_SUBTYPE_ARM64_V8,
	CPU_SUBTYPE_ARM64E,
	CPU_SUBTYPE_ARM_V4T = 5,
	CPU_SUBTYPE_ARM_V6,
	CPU_SUBTYPE_ARM_V5,
	CPU_SUBTYPE_ARM_V5TEJ = CPU_SUBTYPE_ARM_V5,
	CPU_SUBTYPE_ARM_XSCALE,
	CPU_SUBTYPE_ARM_V7,
	CPU_SUBTYPE_ARM_V7S = 11,
	CPU_SUBTYPE_ARM_V7K,
	CPU_SUBTYPE_ARM_V6M = 14,
	CPU_SUBTYPE_ARM_V7M,
	CPU_SUBTYPE_ARM_V7EM
};

enum CPUSubTypePPC {
	CPU_SUBTYPE_POWERPC_ALL = 0,
	CPU_SUBTYPE_POWERPC_601,
	CPU_SUBTYPE_POWERPC_602,
	CPU_SUBTYPE_POWERPC_603,
	CPU_SUBTYPE_POWERPC_603e,
	CPU_SUBTYPE_POWERPC_603ev,
	CPU_SUBTYPE_POWERPC_604,
	CPU_SUBTYPE_POWERPC_604e,
	CPU_SUBTYPE_POWERPC_620,
	CPU_SUBTYPE_POWERPC_750,
	CPU_SUBTYPE_POWERPC_7400,
	CPU_SUBTYPE_POWERPC_7450,
	CPU_SUBTYPE_POWERPC_970 = 100,

	CPU_SUBTYPE_MC980000_ALL = CPU_SUBTYPE_POWERPC_ALL,
	CPU_SUBTYPE_MC98601 = CPU_SUBTYPE_POWERPC_601
};

enum MachOFileType {
	MH_OBJECT = 0x1,
	MH_EXECUTE = 0x2,
	MH_FVMLIB = 0x3,
	MH_CORE = 0x4,
	MH_PRELOAD = 0x5,
	MH_DYLIB = 0x6,
	MH_DYLINKER = 0x7,
	MH_BUNDLE = 0x8,
	MH_DYLIB_STUB = 0x9,
	MH_DSYM = 0xA,
	MH_KEXT_BUNDLE = 0xB,
	MH_FILESET = 0xC
};

static const uint32_t LC_REQ_DYLD = 0x80000000u; // required load command flag

enum MachOLoadCommand {
	LC_SEGMENT = 1,
	LC_SYMTAB,
	LC_SYMSEG,
	LC_THREAD,
	LC_UNIXTHREAD,
	LC_LOADFVMLIB,
	LC_IDFVMLIB,
	LC_IDENT,
	LC_FVMFILE,
	LC_PREPAGE,
	LC_DYSYMTAB,
	LC_LOAD_DYLIB,
	LC_ID_DYLIB,
	LC_LOAD_DYLINKER,
	LC_ID_DYLINKER,
	LC_PREBOUND_DYLIB,
	LC_ROUTINES,
	LC_SUB_FRAMEWORK,
	LC_SUB_UMBRELLA,
	LC_SUB_CLIENT,
	LC_SUB_LIBRARY,
	LC_TWOLEVEL_HINTS,
	LC_PREBIND_CKSUM,
	LC_LOAD_WEAK_DYLIB,
	LC_SEGMENT_64,
	LC_ROUTINES_64,
	LC_UUID,
	LC_RPATH,
	LC_CODE_SIGNATURE,
	LC_SEGMENT_SPLIT_INFO,
	LC_REEXPORT_DYLIB,
	LC_LAZY_LOAD_DYLIB,
	LC_ENCRYPTION_INFO,
	LC_DYLD_INFO,
	LC_DYLD_INFO_ONLY = LC_DYLD_INFO,
	LC_LOAD_UPWARD_DYLIB,
	LC_VERSION_MIN_MACOSX,
	LC_VERSION_MIN_IPHONEOS,
	LC_FUNCTION_STARTS,
	LC_DYLD_ENVIRONMENT,
	LC_MAIN,
	LC_DATA_IN_CODE,
	LC_SOURCE_VERSION,
	LC_DYLIB_CODE_SIGN_DRS,
	LC_ENCRYPTION_INFO_64,
	LC_LINKER_OPTION,
	LC_LINKER_OPTIMIZATION_HINT,
	LC_VERSION_MIN_TVOS,
	LC_VERSION_MIN_WATCHOS,
	LC_NOTE,
	LC_BUILD_VERSION,
	LC_DYLD_EXPORTS_TRIE,
	LC_DYLD_CHAINED_FIXUPS,
	LC_FILESET_ENTRY,
	LC_ATOM_INFO
};

enum MachoPlatform {
	PLATFORM_UNKNOWN = 0,
	PLATFORM_MACOS,
	PLATFORM_IOS,
	PLATFORM_TVOS,
	PLATFORM_WATCHOS,
	PLATFORM_BRIDGEOS,
	PLATFORM_MACCATALYST,
	PLATFORM_IOSSIMULATOR,
	PLATFORM_TVOSSIMULATOR,
	PLATFORM_WATCHOSSIMULATOR,
	PLATFORM_DRIVERKIT,
	PLATFORM_XROS,
	PLATFORM_XROS_SIMULATOR
};

enum MachoTool { TOOL_CLANG = 1, TOOL_SWIFT, TOOL_LD, TOOL_LLD };

/**** Unpacked structures ****/

typedef struct cpu_type_subtype {
	enum CPUType type;
	bool type_is64;
	bool type_is64_32;
	union {
		enum CPUSubTypeX86 subtype_x86;
		enum CPUSubTypeARM subtype_arm;
		enum CPUSubTypePPC subtype_ppc;
	};
	bool subtype_islib64;
} cpu_type_subtype_t;

typedef struct fat_arch {
	struct cpu_type_subtype cpu;
	uint64_t offset;
	uint64_t size;
	uint_fast8_t align;
} fat_arch_t;

typedef struct macho_file {
	uint32_t magic;
	uint32_t narch;
	fat_arch_t arch[];
} macho_file_t;

typedef struct macho_header {
	uint32_t magic;
	bool swap;
	cpu_type_subtype_t cpu;
	enum MachOFileType filetype;
	uint32_t ncmds;
	uint32_t sizeofcmds;
	uint32_t flags;
} macho_header_t;

typedef struct macho_version {
	uint_fast16_t major;
	uint_fast16_t minor;
	uint_fast16_t patch;
} macho_version_t;

typedef struct tool_version {
	enum MachoTool tool;
	macho_version_t version;
} tool_version_t;

typedef struct build_version {
	enum MachoPlatform platform;
	macho_version_t minos;
	macho_version_t sdk;
	uint32_t ntools;
	tool_version_t tools[];
} build_version_t;

typedef struct dylib {
	uint32_t timestamp;
	macho_version_t current_version;
	macho_version_t compatibility_version;
	char path[];
} dylib_t;

/**** Function prototypes ****/

/* utility */
int map_platform_to_darwin(macho_version_t *darwin,
    const enum MachoPlatform platform, const macho_version_t version);

/* readers */
ssize_t read_macho_file(const int fd, macho_file_t **dest);
ssize_t read_macho_header(const int fd, macho_header_t *dest);
ssize_t read_build_version(const int fd, const bool swap,
    build_version_t **dest);
ssize_t read_min_version(const int fd, const bool swap, const uint32_t loadcmd,
    build_version_t **dest);
ssize_t read_path(const int fd, const bool swap, const uint32_t loadcmdsize, 
    char **dest);
ssize_t read_dylib(const int fd, const bool swap, const uint32_t loadcmdsize,
    dylib_t **dest);

#define READ(f, var)                              \
	if ((x = read_##f(fd, swap, &var)) < 0) { \
		return x;                         \
	}                                         \
	n += x
ssize_t read_u32(const int fd, const bool swap, uint32_t *dest);

#endif