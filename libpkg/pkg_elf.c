/*-
 * Copyright (c) 2011-2024 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#elif HAVE_ENDIAN_H
#include <endian.h>
#elif HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#endif
#include <sys/types.h>
#if defined(HAVE_SYS_ELF_COMMON_H) && !defined(__DragonFly__)
#include <sys/elf_common.h>
#endif
#include <sys/stat.h>

#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <gelf.h>
#include <libgen.h>
#if defined(HAVE_LINK_H) && !defined(__DragonFly__) && defined(HAVE_LIBELF)
#include <link.h>
#endif
#include <paths.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_LIBELF
#include <libelf.h>
#endif

#include <bsd_compat.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/pkg_abi.h"
#include "private/event.h"
#include "private/binfmt.h"

#ifndef NT_ABI_TAG
#define NT_ABI_TAG 1
#endif

#define NT_VERSION	1
#define NT_ARCH		2
#define NT_GNU_ABI_TAG	1

#ifndef roundup2
#define roundup2(x, y)	(((x)+((y)-1))&(~((y)-1))) /* if y is powers of two */
#endif

static void elf_parse_abi(Elf *elf, GElf_Ehdr *ehdr, struct pkg_abi *abi);

#ifndef HAVE_ELF_NOTE
typedef Elf32_Nhdr Elf_Note;
#endif

static int
analyse_elf(struct pkg *pkg, const char *fpath)
{
	int ret = EPKG_OK;

	pkg_debug(1, "analysing elf %s", fpath);

	struct stat sb;
	if (lstat(fpath, &sb) != 0)
		pkg_emit_errno("fstat() failed for", fpath);
	/* ignore empty files and non regular files */
	if (sb.st_size == 0 || !S_ISREG(sb.st_mode))
		return (EPKG_END); /* Empty file or sym-link: no results */

	int fd = open(fpath, O_RDONLY, 0);
	if (fd < 0) {
		return (EPKG_FATAL);
	}

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pkg_emit_error("ELF library initialization failed: %s",
		    elf_errmsg(-1));
		return (EPKG_FATAL);
	}

	Elf *e = elf_begin(fd, ELF_C_READ, NULL);
	if (e == NULL) {
		ret = EPKG_FATAL;
		pkg_debug(1, "elf_begin() for %s failed: %s", fpath,
		    elf_errmsg(-1));
		goto cleanup;
	}

	if (elf_kind(e) != ELF_K_ELF) {
		/* Not an elf file: no results */
		ret = EPKG_END;
		pkg_debug(1, "not an elf");
		goto cleanup;
	}

	if (ctx.developer_mode)
		pkg->flags |= PKG_CONTAINS_ELF_OBJECTS;

	GElf_Ehdr elfhdr;
	if (gelf_getehdr(e, &elfhdr) == NULL) {
		ret = EPKG_WARN;
		pkg_debug(1, "getehdr() failed: %s.", elf_errmsg(-1));
		goto cleanup;
	}

	if (elfhdr.e_type != ET_DYN && elfhdr.e_type != ET_EXEC &&
	    elfhdr.e_type != ET_REL) {
		pkg_debug(1, "not an elf");
		ret = EPKG_END;
		goto cleanup;
	}

	/* Parse the needed information from the dynamic section header */
	Elf_Scn *scn = NULL;
	Elf_Scn *dynamic = NULL;
	size_t numdyn = 0;
	size_t sh_link = 0;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		GElf_Shdr shdr;
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			ret = EPKG_FATAL;
			pkg_emit_error("getshdr() for %s failed: %s", fpath,
					elf_errmsg(-1));
			goto cleanup;
		}
		if (shdr.sh_type == SHT_DYNAMIC) {
			dynamic = scn;
			sh_link = shdr.sh_link;
			if (shdr.sh_entsize == 0) {
				ret = EPKG_END;
				goto cleanup;
			}
			numdyn = shdr.sh_size / shdr.sh_entsize;
			break;
		}
	}

	if (dynamic == NULL) {
		ret = EPKG_END;
		goto cleanup; /* not a dynamically linked elf: no results */
	}

	/* A shared object for use with dlopen(3) may lack a NOTE section and
           will therefore have unknown elf_abi.os. */
	struct pkg_abi elf_abi;
	elf_parse_abi(e, &elfhdr, &elf_abi);
	if (elf_abi.os == PKG_OS_UNKNOWN || elf_abi.arch == PKG_ARCH_UNKNOWN) {
		ret = EPKG_END;
		goto cleanup;
	}

	enum pkg_shlib_flags flags = pkg_shlib_flags_from_abi(&elf_abi);
	if ((flags & PKG_SHLIB_FLAGS_COMPAT_LINUX) == 0 && elf_abi.os != ctx.abi.os) {
		ret = EPKG_END;
		goto cleanup; /* Incompatible OS */
	}
	if ((flags & PKG_SHLIB_FLAGS_COMPAT_32) == 0 && elf_abi.arch != ctx.abi.arch) {
		ret = EPKG_END;
		goto cleanup; /* Incompatible architecture */
	}

	Elf_Data *data = elf_getdata(dynamic, NULL);
	if (data == NULL) {
		ret = EPKG_END; /* Some error occurred, ignore this file */
		goto cleanup;
	}

	for (size_t dynidx = 0; dynidx < numdyn; dynidx++) {
		GElf_Dyn *dyn, dyn_mem;
		if ((dyn = gelf_getdyn(data, dynidx, &dyn_mem)) == NULL) {
			ret = EPKG_FATAL;
			pkg_emit_error("getdyn() failed for %s: %s", fpath,
			    elf_errmsg(-1));
			goto cleanup;
		}

		const char *shlib = elf_strptr(e, sh_link, dyn->d_un.d_val);
		if (shlib == NULL || *shlib == '\0') {
			continue;
		}



		if (dyn->d_tag == DT_SONAME) {
			pkg_addshlib_provided(pkg, shlib, flags);
		} else if (dyn->d_tag == DT_NEEDED) {
			pkg_addshlib_required(pkg, shlib, flags);
		}
	}

cleanup:
	if (e != NULL)
		elf_end(e);
	close(fd);

	return (ret);
}

static int
analyse_fpath(struct pkg *pkg, const char *fpath)
{
	const char *dot;

	dot = strrchr(fpath, '.');

	if (dot == NULL)	/* No extension */
		return (EPKG_OK);

	if (dot[1] == 'a' && dot[2] == '\0')
		pkg->flags |= PKG_CONTAINS_STATIC_LIBS;

	if ((dot[1] == 'l' && dot[2] == 'a' && dot[3] == '\0'))
		pkg->flags |= PKG_CONTAINS_LA;

	return (EPKG_OK);
}

static enum pkg_arch
aeabi_parse_arm_attributes(void *data, size_t length)
{
	uint32_t sect_len;
	uint8_t *section = data;

#define	MOVE(len) do {		\
	assert(length >= (len)); \
	section += (len);	\
	length -= (len);	\
} while (0)

	if (length == 0 || *section != 'A')
		return (PKG_ARCH_UNKNOWN);
	MOVE(1);

	/* Read the section length */
	if (length < sizeof(sect_len))
		return (PKG_ARCH_UNKNOWN);
	memcpy(&sect_len, section, sizeof(sect_len));

	/*
	 * The section length should be no longer than the section it is within
	 */
	if (sect_len > length)
		return (PKG_ARCH_UNKNOWN);

	MOVE(sizeof(sect_len));

	/* Skip the vendor name */
	while (length != 0) {
		if (*section == '\0')
			break;
		MOVE(1);
	}
	if (length == 0)
		return (PKG_ARCH_UNKNOWN);
	MOVE(1);

	while (length != 0) {
		uint32_t tag_length;

		switch(*section) {
		case 1: /* Tag_File */
			MOVE(1);
			if (length < sizeof(tag_length))
				return (PKG_ARCH_UNKNOWN);
			memcpy(&tag_length, section, sizeof(tag_length));
			break;
		case 2: /* Tag_Section */
		case 3: /* Tag_Symbol */
		default:
			return (PKG_ARCH_UNKNOWN);
		}
		/* At least space for the tag and size */
		if (tag_length <= 5)
			return (PKG_ARCH_UNKNOWN);
		tag_length--;
		/* Check the tag fits */
		if (tag_length > length)
			return (PKG_ARCH_UNKNOWN);

#define	MOVE_TAG(len) do {		\
	assert(tag_length >= (len));	\
	MOVE(len);			\
	tag_length -= (len);		\
} while(0)

		MOVE(sizeof(tag_length));
		tag_length -= sizeof(tag_length);

		while (tag_length != 0) {
			uint8_t tag;

			assert(tag_length >= length);

			tag = *section;
			MOVE_TAG(1);

			/*
			 * These tag values come from:
			 *
			 * Addenda to, and Errata in, the ABI for the
			 * ARM Architecture. Release 2.08, section 2.3.
			 */
			if (tag == 6) { /* == Tag_CPU_arch */
				uint8_t val;

				val = *section;
				/*
				 * We don't support values that require
				 * more than one byte.
				 */
				if (val & (1 << 7))
					return (PKG_ARCH_UNKNOWN);

				/* We have an ARMv4 or ARMv5 */
				if (val <= 5)
					return (PKG_ARCH_UNKNOWN);
				else if (val == 6) /* We have an ARMv6 */
					return (PKG_ARCH_ARMV6);
				else /* We have an ARMv7+ */
					return (PKG_ARCH_ARMV7);
			} else if (tag == 4 || tag == 5 || tag == 32 ||
			    tag == 65 || tag == 67) {
				while (*section != '\0' && length != 0)
					MOVE_TAG(1);
				if (tag_length == 0)
					return (PKG_ARCH_UNKNOWN);
				/* Skip the last byte */
				MOVE_TAG(1);
			} else if ((tag >= 7 && tag <= 31) || tag == 34 ||
			    tag == 36 || tag == 38 || tag == 42 || tag == 44 ||
			    tag == 64 || tag == 66 || tag == 68 || tag == 70) {
				/* Skip the uleb128 data */
				while (*section & (1 << 7) && length != 0)
					MOVE_TAG(1);
				if (tag_length == 0)
					return (PKG_ARCH_UNKNOWN);
				/* Skip the last byte */
				MOVE_TAG(1);
			} else
				return (PKG_ARCH_UNKNOWN);
#undef MOVE_TAG
		}

		break;
	}
	return (PKG_ARCH_UNKNOWN);
#undef MOVE
}

static enum pkg_arch
elf_parse_arch(Elf *elf, GElf_Ehdr *ehdr)
{
	switch (ehdr->e_machine) {
	case EM_386:
		return (PKG_ARCH_I386);
	case EM_X86_64:
		return (PKG_ARCH_AMD64);
	case EM_AARCH64:
		return (PKG_ARCH_AARCH64);
	case EM_ARM:
		/* Only support EABI */
		if ((ehdr->e_flags & EF_ARM_EABIMASK) == 0) {
			return (PKG_ARCH_UNKNOWN);
		}

		size_t shstrndx;
		elf_getshdrstrndx(elf, &shstrndx);

		GElf_Shdr shdr;
		Elf_Scn *scn = NULL;
		while ((scn = elf_nextscn(elf, scn)) != NULL) {
			if (gelf_getshdr(scn, &shdr) != &shdr) {
				break;
			}
			const char *sh_name = elf_strptr(elf, shstrndx, shdr.sh_name);
			if (sh_name == NULL) {
				continue;
			}
			if (STREQ(".ARM.attributes", sh_name)) {
				Elf_Data *data = elf_getdata(scn, NULL);
				return (aeabi_parse_arm_attributes(data->d_buf, data->d_size));
			}
		}
		break;
	case EM_PPC:
		return (PKG_ARCH_POWERPC);
	case EM_PPC64:
		switch (ehdr->e_ident[EI_DATA]) {
		case ELFDATA2MSB:
			return (PKG_ARCH_POWERPC64);
		case ELFDATA2LSB:
			return (PKG_ARCH_POWERPC64LE);
		}
		break;
	case EM_RISCV:
		switch (ehdr->e_ident[EI_CLASS]) {
		case ELFCLASS32:
			return (PKG_ARCH_RISCV32);
		case ELFCLASS64:
			return (PKG_ARCH_RISCV64);
		}
		break;
	}

	return (PKG_ARCH_UNKNOWN);
}

/* Returns true if the OS and version were successfully parsed */
static bool
elf_note_analyse(Elf_Data *data, GElf_Ehdr *elfhdr, struct pkg_abi *abi)
{
	Elf_Note note;
	char *src;
	uint32_t gnu_abi_tag[4];
	int note_ost[6] = {
		PKG_OS_LINUX,
		PKG_OS_UNKNOWN, /* GNU Hurd */
		PKG_OS_UNKNOWN, /* Solaris */
		PKG_OS_FREEBSD,
		PKG_OS_NETBSD,
		PKG_OS_UNKNOWN, /* Syllable */
	};
	uint32_t version = 0;
	int version_style = 1;

	src = data->d_buf;

	while ((uintptr_t)src < ((uintptr_t)data->d_buf + data->d_size)) {
		memcpy(&note, src, sizeof(Elf_Note));
		src += sizeof(Elf_Note);
		if ((strncmp ((const char *) src, "FreeBSD", note.n_namesz) == 0) ||
		    (strncmp ((const char *) src, "DragonFly", note.n_namesz) == 0) ||
		    (strncmp ((const char *) src, "NetBSD", note.n_namesz) == 0) ||
		    (note.n_namesz == 0)) {
			if (note.n_type == NT_VERSION) {
				version_style = 1;
				break;
			}
		}
		if (strncmp ((const char *) src, "GNU", note.n_namesz) == 0) {
			if (note.n_type == NT_GNU_ABI_TAG) {
				version_style = 2;
				break;
			}
		}
		src += roundup2(note.n_namesz + note.n_descsz, 4);
	}
	if ((uintptr_t)src >= ((uintptr_t)data->d_buf + data->d_size)) {
		return (false);
	}
	if (version_style == 2) {
		/*
		 * NT_GNU_ABI_TAG
		 * Operating system (OS) ABI information.  The
		 * desc field contains 4 words:
		 * word 0: OS descriptor (ELF_NOTE_OS_LINUX, ELF_NOTE_OS_GNU, etc)
		 * word 1: major version of the ABI
		 * word 2: minor version of the ABI
		 * word 3: subminor version of the ABI
		 */
		src += roundup2(note.n_namesz, 4);
		if (elfhdr->e_ident[EI_DATA] == ELFDATA2MSB) {
			for (int wdndx = 0; wdndx < 4; wdndx++) {
				gnu_abi_tag[wdndx] = be32dec(src);
				src += 4;
			}
		} else {
			for (int wdndx = 0; wdndx < 4; wdndx++) {
				gnu_abi_tag[wdndx] = le32dec(src);
				src += 4;
			}
		}
		if (gnu_abi_tag[0] < 6) {
			abi->os= note_ost[gnu_abi_tag[0]];
		} else {
			abi->os = PKG_OS_UNKNOWN;
		}
	} else {
		if (note.n_namesz == 0) {
			abi->os = PKG_OS_UNKNOWN;
		} else {
			if (STREQ(src, "FreeBSD"))
				abi->os = PKG_OS_FREEBSD;
			else if (STREQ(src, "DragonFly"))
				abi->os = PKG_OS_DRAGONFLY;
			else if (STREQ(src, "NetBSD"))
				abi->os = PKG_OS_NETBSD;
		}
		src += roundup2(note.n_namesz, 4);
		if (elfhdr->e_ident[EI_DATA] == ELFDATA2MSB)
			version = be32dec(src);
		else
			version = le32dec(src);
	}

	if (version_style == 2) {
		if (abi->os == PKG_OS_LINUX) {
			abi->major = gnu_abi_tag[1];
			abi->minor = gnu_abi_tag[2];
		} else {
			abi->major = gnu_abi_tag[1];
			abi->minor = gnu_abi_tag[2];
			abi->patch = gnu_abi_tag[3];
		}
	} else {
		switch (abi->os) {
		case PKG_OS_UNKNOWN:
			break;
		case PKG_OS_FREEBSD:
			pkg_abi_set_freebsd_osversion(abi, version);
			break;
		case PKG_OS_DRAGONFLY:
			abi->major = version / 100000;
			abi->minor = (((version / 100 % 1000)+1)/2)*2;
			break;
		case PKG_OS_NETBSD:
			abi->major = (version + 1000000) / 100000000;
			break;
		default:
			assert(0);
		}
	}

	return (true);
}

static void
elf_parse_abi(Elf *elf, GElf_Ehdr *ehdr, struct pkg_abi *abi)
{
	*abi = (struct pkg_abi){0};

	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		GElf_Shdr shdr;
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			pkg_emit_error("getshdr() failed: %s.", elf_errmsg(-1));
			return;
		}

		if (shdr.sh_type == SHT_NOTE) {
			Elf_Data *data = elf_getdata(scn, NULL);
			/*
			 * loop over all the note section and override what
			 * should be overridden if any
			 */
			elf_note_analyse(data, ehdr, abi);
		}
	}

	abi->arch = elf_parse_arch(elf, ehdr);
}

int
pkg_elf_abi_from_fd(int fd, struct pkg_abi *abi)
{
	Elf *elf = NULL;
	GElf_Ehdr elfhdr;
	int ret = EPKG_OK;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pkg_emit_error("ELF library initialization failed: %s",
		    elf_errmsg(-1));
		return (EPKG_FATAL);
	}

	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		ret = EPKG_FATAL;
		pkg_emit_error("elf_begin() failed: %s.", elf_errmsg(-1));
		goto cleanup;
	}

	if (gelf_getehdr(elf, &elfhdr) == NULL) {
		ret = EPKG_WARN;
		pkg_debug(1, "getehdr() failed: %s.", elf_errmsg(-1));
		goto cleanup;
	}

	elf_parse_abi(elf, &elfhdr, abi);

	if (abi->os == PKG_OS_UNKNOWN) {
		ret = EPKG_FATAL;
		pkg_emit_error("failed to determine the operating system");
		goto cleanup;
	}

	if (abi->arch == PKG_ARCH_UNKNOWN) {
		ret = EPKG_FATAL;
		pkg_emit_error("failed to determine the architecture");
		goto cleanup;
	}

cleanup:
	if (elf != NULL)
		elf_end(elf);
	return (ret);
}

int pkg_analyse_init_elf(__unused const char* stage) {
	if (elf_version(EV_CURRENT) == EV_NONE)
		return (EPKG_FATAL);
	return (EPKG_OK);
}

int pkg_analyse_elf(const bool developer_mode, struct pkg *pkg, const char *fpath)
{
	int ret = analyse_elf(pkg, fpath);
	if (developer_mode) {
		if (ret != EPKG_OK && ret != EPKG_END) {
			return EPKG_WARN;
		}
		analyse_fpath(pkg, fpath);
	}
	return ret;
}

int pkg_analyse_close_elf() {
	return EPKG_OK;
}
