/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
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
#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <ctype.h>
#include <paths.h>
#include <unistd.h>

#include "pkg.h"

#include "private/pkg.h"
#include "private/event.h"

#define _PATH_UNAME "/usr/bin/uname"

int pkg_get_myarch_elfparse(int fd, char *dest, size_t sz, struct os_info *oi);
int pkg_analyse_init_elf(const char* stage);
int pkg_analyse_elf(const bool developer_mode, struct pkg *pkg, const char *fpath);
int pkg_analyse_close_elf();

int pkg_get_myarch_macho(int fd, char *dest, size_t sz, struct os_info *oi);
int pkg_analyse_init_macho(const char* stage);
int pkg_analyse_macho(const bool developer_mode, struct pkg *pkg, const char *fpath);
int pkg_analyse_close_macho();


/* All possibilities on FreeBSD as of 5/26/2014 */
struct arch_trans {
	const char *elftype;
	const char *archid;
};

static struct arch_trans machine_arch_translation[] = {
	{ "x86:32", "i386" },
	{ "x86:64", "amd64" },
	{ "powerpc:32:eb", "powerpc" },
	{ "powerpc:64:eb", "powerpc64" },
	{ "powerpc:64:el", "powerpc64le" },
	{ "sparc64:64", "sparc64" },
	{ "ia64:64", "ia64" },
	/* All the ARM stuff */
	{ "armv6:32:el:eabi:hardfp", "armv6" },
	{ "armv7:32:el:eabi:hardfp", "armv7" },
	{ "aarch64:64", "aarch64" },
	/* And now MIPS */
	{ "mips:32:el:o32", "mipsel" },
	{ "mips:32:el:n32", "mipsn32el" },
	{ "mips:32:eb:o32", "mips" },
	{ "mips:32:eb:n32", "mipsn32" },
	{ "mips:64:el:n64", "mips64el" },
	{ "mips:64:eb:n64", "mips64" },
	/* And RISC-V */
	{ "riscv:32:hf", "riscv32" },
	{ "riscv:32:sf", "riscv32sf" },
	{ "riscv:64:hf", "riscv64" },
	{ "riscv:64:sf", "riscv64sf" },

	{ NULL, NULL }
};

static int
pkg_get_myarch_fromfile(char *dest, size_t sz, struct os_info *oi)
{
	char rooted_abi_file[PATH_MAX];
	const char *abi_files[] = {
		getenv("ABI_FILE"),
		_PATH_UNAME,
		_PATH_BSHELL,
	};
	struct os_info loi;
	int i, fd;

	if (oi == NULL) {
		memset(&loi, 0, sizeof(loi));
		oi = &loi;
	}

	/*
	 * Perhaps not yet needed, but it may be in the future that there's no
	 * need to check root under some conditions where there is a rootdir.
	 * This also helps alleviate some excessive wrapping later.
	 */
	bool checkroot = ctx.pkg_rootdir != NULL;
	for (fd = -1, i = 0; i < NELEM(abi_files); i++) {
		if (abi_files[i] == NULL)
			continue;
		/*
		 * Try prepending rootdir and using that if it exists.  If
		 * ABI_FILE is specified, assume that the consumer didn't want
		 * it mangled by rootdir.
		 */
		if (i > 0 && checkroot && snprintf(rooted_abi_file, PATH_MAX,
		    "%s/%s", ctx.pkg_rootdir, abi_files[i]) < PATH_MAX) {
			if ((fd = open(rooted_abi_file, O_RDONLY)) >= 0)
				break;
		}
		if ((fd = open(abi_files[i], O_RDONLY)) >= 0)
			break;
		/* if the ABI_FILE was provided we only care about it */
		if (i == 0)
			break;
	}
	if (fd == -1) {
		pkg_emit_error("Unable to determine the ABI\n");
		return (EPKG_FATAL);
	}

	int ret = pkg_get_myarch_elfparse(fd, dest, sz, oi);
	if (EPKG_OK != ret) {
		lseek(fd, 0, SEEK_SET);
		ret = pkg_get_myarch_macho(fd, dest, sz, oi);
	}
	
	if (oi == &loi) {
		free(oi->name);
		free(oi->version);
		free(oi->version_major);
		free(oi->version_minor);
		free(oi->arch);
	}
	close(fd);
	return ret;
}

int
pkg_get_myarch_with_legacy(char *dest, char* dest_legacy, size_t sz, struct os_info *oi)
{
	int err = pkg_get_myarch_fromfile(dest_legacy, sz, oi);
	if (err) {
		if (oi) {
			free(oi->name);
		}
		return (err);
	}
	strlcpy(dest, dest_legacy, sz);

    for(char *p = dest_legacy; *p; ++p) {
        *p = tolower(*p);
    }    

// TODO: When dealing with DragonFly, not only on DragonFly
#ifdef __DragonFly__
	size_t dsz;

	dsz = strlen(dest);
	if (strncasecmp(dest, "DragonFly", 9) == 0) {
		for (int i = 0; i < dsz; i++)
			dest[i] = tolower(dest[i]);
		return (0);
	}
#endif

	/* Translate architecture string back to regular OS one */
	char *arch_tweak = strchr(dest, ':');
	if (arch_tweak == NULL)
		return (0);
	arch_tweak++;
	arch_tweak = strchr(arch_tweak, ':');
	if (arch_tweak == NULL)
		return (0);
	arch_tweak++;
	for (struct arch_trans *arch_trans = machine_arch_translation; arch_trans->elftype != NULL;
	    arch_trans++) {
		if (STREQ(arch_tweak, arch_trans->elftype)) {
			strlcpy(arch_tweak, arch_trans->archid,
			    sz - (arch_tweak - dest));
			oi->arch = xstrdup(arch_tweak);
			break;
		}
	}

	return (0);
}

int
pkg_arch_to_legacy(const char *arch, char *dest, size_t sz)
{
	int i = 0;
	struct arch_trans *arch_trans;

	memset(dest, '\0', sz);
	/* Lower case the OS */
	while (arch[i] != ':' && arch[i] != '\0') {
		dest[i] = tolower(arch[i]);
		i++;
	}
	if (arch[i] == '\0')
		return (0);

	dest[i++] = ':';

	/* Copy the version */
	while (arch[i] != ':' && arch[i] != '\0') {
		dest[i] = arch[i];
		i++;
	}
	if (arch[i] == '\0')
		return (0);

	dest[i++] = ':';

	for (arch_trans = machine_arch_translation; arch_trans->elftype != NULL;
	    arch_trans++) {
		if (STREQ(arch + i, arch_trans->archid)) {
			strlcpy(dest + i, arch_trans->elftype,
			    sz - (arch + i - dest));
			return (0);
		}
	}
	strlcpy(dest + i, arch + i, sz - (arch + i  - dest));

	return (0);
}

int
pkg_analyse_files(struct pkgdb *db __unused, struct pkg *pkg, const char *stage)
{
	struct pkg_file *file = NULL;
	int ret = EPKG_OK;
	char fpath[MAXPATHLEN +1];
	const char *lib;
	bool failures = false;

	int (*pkg_analyse_init)(const char* stage)  = pkg_analyse_init_elf;
	int (*pkg_analyse)(const bool developer_mode, struct pkg *pkg, const char *fpath) = pkg_analyse_elf;
	int (*pkg_analyse_close)()  = pkg_analyse_close_elf;

	if (tll_length(pkg->shlibs_required) != 0) {
		tll_free_and_free(pkg->shlibs_required, free);
	}

	if (tll_length(pkg->shlibs_provided) != 0) {
		tll_free_and_free(pkg->shlibs_provided, free);
	}

	ret = pkg_analyse_init(stage);
	if (ret != EPKG_OK) {
		goto cleanup;
	}

	/* Assume no architecture dependence, for contradiction */
	if (ctx.developer_mode)
		pkg->flags &= ~(PKG_CONTAINS_ELF_OBJECTS |
				PKG_CONTAINS_STATIC_LIBS |
				PKG_CONTAINS_LA);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (stage != NULL)
			snprintf(fpath, sizeof(fpath), "%s/%s", stage, file->path);
		else
			strlcpy(fpath, file->path, sizeof(fpath));

		ret = pkg_analyse(ctx.developer_mode, pkg, fpath);
		if (EPKG_WARN == ret) {
			failures = true;
		}
	}

	/*
	 * Do not depend on libraries that a package provides itself
	 */
	tll_foreach(pkg->shlibs_required, s) {
		if (stringlist_contains(&pkg->shlibs_provided, s->item)) {
			pkg_debug(2, "remove %s from required shlibs as the "
			    "package %s provides this library itself",
			    s->item, pkg->name);
			tll_remove_and_free(pkg->shlibs_required, s, free);
			continue;
		}
		file = NULL;
		while (pkg_files(pkg, &file) == EPKG_OK) {
			if ((lib = strstr(file->path, s->item)) != NULL &&
			    strlen(lib) == strlen(s->item) && lib[-1] == '/') {
				pkg_debug(2, "remove %s from required shlibs as "
				    "the package %s provides this file itself",
				    s->item, pkg->name);

				tll_remove_and_free(pkg->shlibs_required, s, free);
				break;
			}
		}
	}

	/*
	 * if the package is not supposed to provide share libraries then
	 * drop the provided one
	 */
	if (pkg_kv_get(&pkg->annotations, "no_provide_shlib") != NULL) {
		tll_free_and_free(pkg->shlibs_provided, free);
	}

	if (failures)
		goto cleanup;

	ret = EPKG_OK;

cleanup:
	ret = pkg_analyse_close();

	return (ret);
}