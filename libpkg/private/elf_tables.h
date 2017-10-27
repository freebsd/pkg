/*-
 * Copyright (c) 2012 Olivier Houchard <cognet@FreeBSD.org>
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
#ifndef ELF_TABLES_H_
#define ELF_TABLES_H_
struct _elf_corres {
	int elf_nb;
	const char *string;
};

static const struct _elf_corres mach_corres[] = {
	{ EM_386, "x86" },
	{ EM_X86_64, "x86" },
#ifdef EM_AARCH64
	{ EM_AARCH64, "aarch64" },
#endif
	{ EM_ARM, "arm" },
	{ EM_MIPS, "mips" },
	{ EM_PPC, "powerpc" },
	{ EM_PPC64, "powerpc" },
	{ EM_SPARCV9, "sparc64" },
	{ EM_IA_64, "ia64" },
	{ -1, NULL },
};

static const struct _elf_corres wordsize_corres[] = {
	{ ELFCLASS32, "32" },
	{ ELFCLASS64, "64" },
	{ -1, NULL},
};

static const struct _elf_corres endian_corres[] = {
	{ ELFDATA2MSB, "eb" },
	{ ELFDATA2LSB, "el" },
	{ -1, NULL}
};

static const struct _elf_corres os_corres[] = {
	{ ELFOSABI_FREEBSD, "freebsd" },
	{ -1, NULL }
};

#ifndef EF_MIPS_ABI
#define EF_MIPS_ABI	0x0000F000
#endif
#ifndef EF_ARM_VFP_FLOAT
#define EF_ARM_VFP_FLOAT	0x00000400
#endif
#define E_MIPS_ABI_O32	0x00001000
#define E_MIPS_ABI_N32	0x00000020

#define NT_VERSION	1
#define NT_ARCH	2
#define NT_GNU_ABI_TAG	1

/* All possibilities on FreeBSD as of 5/26/2014 */
struct arch_trans {
	const char *elftype;
	const char *archid;
};

static struct arch_trans machine_arch_translation[] = {
	{ "x86:32", "i386" },
	{ "x86:64", "amd64" },
	{ "powerpc:32", "powerpc" },
	{ "powerpc:64", "powerpc64" },
	{ "sparc64:64", "sparc64" },
	{ "ia64:64", "ia64" },
	/* All the ARM stuff */
	{ "arm:32:el:eabi:softfp", "arm" },
	{ "arm:32:el:oabi:softfp", "arm" },
	{ "arm:32:eb:eabi:softfp", "armeb" },
	{ "arm:32:eb:oabi:softfp", "armeb" },
	{ "armv6:32:el:eabi:softfp", "armv6" },
	{ "armv6:32:el:eabi:hardfp", "armv6" },
	{ "armv7:32:el:eabi:softfp", "armv7" },
	{ "armv7:32:el:eabi:hardfp", "armv7" },
	{ "aarch64:64", "aarch64" },
	/* And now MIPS */
	{ "mips:32:el:o32", "mipsel" },
	{ "mips:32:el:n32", "mipsn32el" },
	{ "mips:32:eb:o32", "mips" },
	{ "mips:32:eb:n32", "mipsn32" },
	{ "mips:64:el:n64", "mips64el" },
	{ "mips:64:eb:n64", "mips64" },

	{ NULL, NULL }
};

#endif /* ELF_TABLES_H_ */
