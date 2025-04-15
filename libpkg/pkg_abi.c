/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2024 The FreeBSD Foundation
 *
 * This software was developed in part by Isaac Freund <ifreund@freebsdfoundation.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <ctype.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg_abi.h"
#include "private/binfmt.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"
#include "xmalloc.h"

#define _PATH_UNAME "/usr/bin/uname"

/* All possibilities on FreeBSD as of 5/26/2014 */
struct arch_trans {
	const char *elftype;
	const char *archid;
};

static struct arch_trans machine_arch_translation[] = { { "x86:32", "i386" },
	{ "x86:64", "amd64" }, { "powerpc:32:eb", "powerpc" },
	{ "powerpc:64:eb", "powerpc64" }, { "powerpc:64:el", "powerpc64le" },
	{ "sparc64:64", "sparc64" }, { "ia64:64", "ia64" },
	/* All the ARM stuff */
	{ "armv6:32:el:eabi:hardfp", "armv6" },
	{ "armv7:32:el:eabi:hardfp", "armv7" }, { "aarch64:64", "aarch64" },
	/* And now MIPS */
	{ "mips:32:el:o32", "mipsel" }, { "mips:32:el:n32", "mipsn32el" },
	{ "mips:32:eb:o32", "mips" }, { "mips:32:eb:n32", "mipsn32" },
	{ "mips:64:el:n64", "mips64el" }, { "mips:64:eb:n64", "mips64" },
	/* And RISC-V */
	{ "riscv:32:hf", "riscv32" }, { "riscv:32:sf", "riscv32sf" },
	{ "riscv:64:hf", "riscv64" }, { "riscv:64:sf", "riscv64sf" },

	{ NULL, NULL } };

static struct {
	enum pkg_os os;
	const char *string;
} os_string_table[] = {
	{ PKG_OS_UNKNOWN, "Unknown" },
	{ PKG_OS_FREEBSD, "FreeBSD" },
	{ PKG_OS_NETBSD, "NetBSD" },
	{ PKG_OS_DRAGONFLY, "dragonfly" },
	{ PKG_OS_LINUX, "Linux" },
	{ PKG_OS_DARWIN, "Darwin" },
	{ -1, NULL },
};

/* This table does not include PKG_ARCH_AMD64 as the string translation of
   that arch is os-dependent. */
static struct {
	enum pkg_arch arch;
	const char *string;
} arch_string_table[] = {
	{ PKG_ARCH_UNKNOWN, "unknown"},
	{ PKG_ARCH_I386, "i386"},
	{ PKG_ARCH_ARMV6, "armv6"},
	{ PKG_ARCH_ARMV7, "armv7"},
	{ PKG_ARCH_AARCH64, "aarch64"},
	{ PKG_ARCH_POWERPC, "powerpc"},
	{ PKG_ARCH_POWERPC64, "powerpc64"},
	{ PKG_ARCH_POWERPC64LE, "powerpc64le"},
	{ PKG_ARCH_RISCV32, "riscv32"},
	{ PKG_ARCH_RISCV64, "riscv64"},
	{ -1, NULL },
};

const char *
pkg_os_to_string(enum pkg_os os)
{
	for (size_t i = 0; os_string_table[i].string != NULL; i++) {
		if (os == os_string_table[i].os) {
			return os_string_table[i].string;
		}
	}
	assert(0);
}

enum pkg_os
pkg_os_from_string(const char *string)
{
	for (size_t i = 0; os_string_table[i].string != NULL; i++) {
		if (STREQ(string, os_string_table[i].string)) {
			return os_string_table[i].os;
		}
	}
	return (PKG_OS_UNKNOWN);
}

/* Returns true if the OS uses "amd64" rather than "x86_64" */
static bool
pkg_os_uses_amd64_name(enum pkg_os os)
{
	switch (os) {
	case PKG_OS_FREEBSD:
		return (true);
	case PKG_OS_DARWIN:
	case PKG_OS_NETBSD:
	case PKG_OS_LINUX:
		return (false);
	case PKG_OS_DRAGONFLY:
	case PKG_OS_UNKNOWN:
	default:
		assert(0);
	}
}

const char *
pkg_arch_to_string(enum pkg_os os, enum pkg_arch arch)
{
	if (arch == PKG_ARCH_AMD64) {
		if (os == PKG_OS_DRAGONFLY) {
			return ("x86:64");
		} else if (pkg_os_uses_amd64_name(os)) {
			return ("amd64");
		} else {
			return ("x86_64");
		}
	}

	for (size_t i = 0; arch_string_table[i].string != NULL; i++) {
		if (arch == arch_string_table[i].arch) {
			return arch_string_table[i].string;
		}
	}

	assert(0);
}

enum pkg_arch
pkg_arch_from_string(enum pkg_os os, const char *string)
{
	if (os == PKG_OS_DRAGONFLY) {
		if (STREQ(string, "x86:64")) {
			return (PKG_ARCH_AMD64);
		}
	} else if (pkg_os_uses_amd64_name(os)) {
		if (STREQ(string, "amd64")) {
			return (PKG_ARCH_AMD64);
		}
	} else {
		if (STREQ(string, "x86_64")) {
			return (PKG_ARCH_AMD64);
		}
	}

	for (size_t i = 0; arch_string_table[i].string != NULL; i++) {
		if (STREQ(string, arch_string_table[i].string)) {
			return arch_string_table[i].arch;
		}
	}

	return (PKG_ARCH_UNKNOWN);
}

bool
pkg_abi_string_only_major_version(enum pkg_os os)
{
	switch (os) {
	case PKG_OS_FREEBSD:
	case PKG_OS_NETBSD:
	case PKG_OS_DARWIN:
		return (true);
	case PKG_OS_DRAGONFLY:
	case PKG_OS_LINUX:
		return (false);
	case PKG_OS_UNKNOWN:
	default:
		assert (0);
	}
}

char *
pkg_abi_to_string(const struct pkg_abi *abi)
{
	char *ret;
	if (pkg_abi_string_only_major_version(abi->os)) {
		xasprintf(&ret, "%s:%d:%s", pkg_os_to_string(abi->os),
		    abi->major, pkg_arch_to_string(abi->os, abi->arch));
	} else {
		xasprintf(&ret, "%s:%d.%d:%s", pkg_os_to_string(abi->os),
		    abi->major, abi->minor,
		    pkg_arch_to_string(abi->os, abi->arch));
	}
	return (ret);
}

bool
pkg_abi_from_string(struct pkg_abi *abi, const char *string)
{
	*abi = (struct pkg_abi){0};

	bool ret = false;

	char *copy = xstrdup(string);

	char *iter = copy;
	char *os = strsep(&iter, ":");
	assert(os != NULL);
	abi->os = pkg_os_from_string(os);
	if (abi->os == PKG_OS_UNKNOWN) {
		pkg_emit_error("Unknown OS '%s' in ABI string", os);
		goto out;
	}

	char *version = strsep(&iter, ":");
	if (version == NULL) {
		pkg_emit_error("Invalid ABI string '%s', "
		    "missing version and architecture", string);
		goto out;
	}
	const char *errstr = NULL;
	if (pkg_abi_string_only_major_version(abi->os)) {
		abi->major = strtonum(version, 1, INT_MAX, &errstr);
	} else {
		/* XXX add tests for this */
		char *major = strsep(&version, ".");
		char *minor = strsep(&version, ".");

		assert(major != NULL);
		if (minor == NULL) {
			pkg_emit_error("Invalid ABI string %s, "
			    "missing minor OS version", string);
			goto out;
		}

		abi->major = strtonum(major, 1, INT_MAX, &errstr);
		if (errstr != NULL) {
			abi->minor = strtonum(minor, 1, INT_MAX, &errstr);
		}
	}
	if (errstr != NULL) {
		pkg_emit_error("Invalid version in ABI string '%s'", string);
		goto out;
	}

	/* DragonFlyBSD continues to use the legacy/altabi format.
	   For example: dragonfly:5.10:x86:64
	   This means we can't use strsep again since that would split the arch
	   string for dragonfly. */
	char *arch = iter;
	if (arch == NULL) {
		pkg_emit_error("Invalid ABI string '%s', "
		    "missing architecture", string);
		goto out;
	}

	abi->arch = pkg_arch_from_string(abi->os, arch);
	if (abi->arch == PKG_ARCH_UNKNOWN) {
		pkg_emit_error("Unknown architecture '%s' in ABI string", arch);
		goto out;
	}

	if (abi->os == PKG_OS_DRAGONFLY && abi->arch != PKG_ARCH_AMD64) {
		pkg_emit_error("Invalid ABI string '%s', "
		    "only x86:64 is supported on dragonfly.", string);
		goto out;
	}

	ret = true;
out:
	free(copy);
	return (ret);
}

void
pkg_abi_set_freebsd_osversion(struct pkg_abi *abi, int osversion)
{
	assert(abi->os == PKG_OS_FREEBSD);

	abi->major = osversion / 100000;
	abi->minor = (osversion / 1000) % 100;
	abi->patch = osversion % 1000;
}

int
pkg_abi_get_freebsd_osversion(struct pkg_abi *abi)
{
	assert(abi->os == PKG_OS_FREEBSD);

	return (abi->major * 100000) + (abi->minor * 1000) + abi->patch;
}

int
pkg_abi_from_file(struct pkg_abi *abi)
{
	char rooted_abi_file[PATH_MAX];
	const char *abi_files[] = {
		getenv("ABI_FILE"),
		_PATH_UNAME,
		_PATH_BSHELL,
	};
	char work_abi_file[PATH_MAX];
	char work_arch_hint[PATH_MAX];

	int i, fd;

	/*
	 * Perhaps not yet needed, but it may be in the future that there's no
	 * need to check root under some conditions where there is a rootdir.
	 * This also helps alleviate some excessive wrapping later.
	 */
	bool checkroot = ctx.pkg_rootdir != NULL;
	for (fd = -1, i = 0; i < NELEM(abi_files); i++) {
		if (abi_files[i] == NULL)
			continue;

		const char *sep = strrchr(abi_files[i], '#');
		if (sep) {
			strlcpy(work_abi_file, abi_files[i],
			    MIN(sep - abi_files[i] + 1, sizeof(work_abi_file)));
			strlcpy(work_arch_hint, sep + 1,
			    sizeof(work_arch_hint));
		} else {
			strlcpy(work_abi_file, abi_files[i],
			    sizeof(work_abi_file));
			work_arch_hint[0] = '\0';
		}

		/*
		 * Try prepending rootdir and using that if it exists.  If
		 * ABI_FILE is specified, assume that the consumer didn't want
		 * it mangled by rootdir.
		 */
		if (i > 0 && checkroot &&
		    snprintf(rooted_abi_file, PATH_MAX, "%s/%s",
			ctx.pkg_rootdir, work_abi_file) < PATH_MAX) {
			if ((fd = open(rooted_abi_file, O_RDONLY)) >= 0) {
				strlcpy(work_abi_file, rooted_abi_file,
				    sizeof(work_abi_file));
				break;
			}
		}
		if ((fd = open(work_abi_file, O_RDONLY)) >= 0) {
			break;
		}
		/* if the ABI_FILE was provided we only care about it */
		if (i == 0)
			break;
	}
	if (fd == -1) {
		pkg_emit_error(
		    "Unable to determine the ABI, none of the ABI_FILEs can be read.");
		return EPKG_FATAL;
	}


	int ret = pkg_elf_abi_from_fd(fd, abi);
	if (EPKG_OK != ret) {
		if (-1 == lseek(fd, 0, SEEK_SET)) {
			pkg_emit_errno("Error seeking file", work_abi_file);
			ret = EPKG_FATAL;
			goto close_out;
		}

		enum pkg_arch arch_hint = PKG_ARCH_UNKNOWN;
		if (work_arch_hint[0]) {
			arch_hint = pkg_arch_from_string(PKG_OS_DARWIN, work_arch_hint);
			if (arch_hint == PKG_ARCH_UNKNOWN) {
				pkg_emit_error("Invalid ABI_FILE architecture hint %s",
				    work_arch_hint);
				ret = EPKG_FATAL;
				goto close_out;
			}
		}

		ret = pkg_macho_abi_from_fd(fd, abi, arch_hint);
		if (EPKG_OK != ret) {
			pkg_emit_error(
			    "Unable to determine ABI, %s cannot be parsed.",
			    work_abi_file);
			ret = EPKG_FATAL;
			goto close_out;
		}
	}

close_out:
	if (close(fd)) {
		pkg_emit_errno("Error closing file", work_abi_file);
		ret = EPKG_FATAL;
	}
	return ret;
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
	strlcpy(dest + i, arch + i, sz - (arch + i - dest));

	return (0);
}

void
pkg_cleanup_shlibs_required(struct pkg *pkg, charv_t *internal_provided)
{
	struct pkg_file *file = NULL;
	const char *lib;

	vec_foreach(pkg->shlibs_required, i) {
		const char *s = pkg->shlibs_required.d[i];
		if (charv_contains(&pkg->shlibs_provided, s, false) ||
		    charv_contains(internal_provided, s, false)) {
			pkg_debug(2,
			    "remove %s from required shlibs as the "
			    "package %s provides this library itself",
			    s, pkg->name);
			vec_remove_and_free(&pkg->shlibs_required, i, free);
			continue;
		}
		if (match_ucl_lists(s,
		    pkg_config_get("SHLIB_REQUIRE_IGNORE_GLOB"),
		    pkg_config_get("SHLIB_REQUIRE_IGNORE_REGEX"))) {
			pkg_debug(2,
			    "remove %s from required shlibs for package %s as it "
			    "is matched by SHLIB_REQUIRE_IGNORE_GLOB/REGEX.",
			    s, pkg->name);
			vec_remove_and_free(&pkg->shlibs_required, i, free);
			continue;
		}
		file = NULL;
		while (pkg_files(pkg, &file) == EPKG_OK) {
			if ((lib = strstr(file->path, s)) != NULL &&
			    strlen(lib) == strlen(s) && lib[-1] == '/') {
				pkg_debug(2,
				    "remove %s from required shlibs as "
				    "the package %s provides this file itself",
				    s, pkg->name);

				vec_remove_and_free(&pkg->shlibs_required, i,
				    free);
				break;
			}
		}
	}
}

int
pkg_analyse_files(struct pkgdb *db __unused, struct pkg *pkg, const char *stage)
{
	struct pkg_file *file = NULL;
	int ret = EPKG_OK;
	char fpath[MAXPATHLEN + 1];
	bool failures = false;

	int (*pkg_analyse_init)(const char *stage);
	int (*pkg_analyse)(const bool developer_mode, struct pkg *pkg,
	    const char *fpath, char **provided, enum pkg_shlib_flags *flags);
	int (*pkg_analyse_close)();

	if (0 == strncmp(pkg->abi, "Darwin", 6)) {
		pkg_analyse_init=pkg_analyse_init_macho;
		pkg_analyse=pkg_analyse_macho;
		pkg_analyse_close=pkg_analyse_close_macho;
	} else {
		pkg_analyse_init=pkg_analyse_init_elf;
		pkg_analyse=pkg_analyse_elf;
		pkg_analyse_close=pkg_analyse_close_elf;
	}

	if (vec_len(&pkg->shlibs_required) != 0) {
		vec_free_and_free(&pkg->shlibs_required, free);
	}

	if (vec_len(&pkg->shlibs_provided) != 0) {
		vec_free_and_free(&pkg->shlibs_provided, free);
	}

	ret = pkg_analyse_init(stage);
	if (ret != EPKG_OK) {
		goto cleanup;
	}

	/* Assume no architecture dependence, for contradiction */
	if (ctx.developer_mode)
		pkg->flags &= ~(PKG_CONTAINS_ELF_OBJECTS |
		    PKG_CONTAINS_STATIC_LIBS | PKG_CONTAINS_LA);

	/* shlibs that are provided by files in the package but not matched by
	   SHLIB_PROVIDE_PATHS_* are still used to filter the shlibs
	   required by the package */
	charv_t internal_provided;
	vec_init(&internal_provided);
	/* list of shlibs that are in the path to be evaluated for provided but are symlinks */
	charv_t maybe_provided;
	vec_init(&maybe_provided);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		struct stat st;
		if (stage != NULL)
			snprintf(fpath, sizeof(fpath), "%s/%s", stage,
			    file->path);
		else
			strlcpy(fpath, file->path, sizeof(fpath));

		char *provided = NULL;
		enum pkg_shlib_flags provided_flags = PKG_SHLIB_FLAGS_NONE;

		ret = pkg_analyse(ctx.developer_mode, pkg, fpath, &provided, &provided_flags);
		if (EPKG_WARN == ret) {
			failures = true;
		}

		if (provided != NULL) {
			const ucl_object_t *paths = NULL;

			switch (provided_flags) {
			case PKG_SHLIB_FLAGS_NONE:
				paths = pkg_config_get("SHLIB_PROVIDE_PATHS_NATIVE");
				break;
			case PKG_SHLIB_FLAGS_COMPAT_32:
				paths = pkg_config_get("SHLIB_PROVIDE_PATHS_COMPAT_32");
				break;
			case PKG_SHLIB_FLAGS_COMPAT_LINUX:
				paths = pkg_config_get("SHLIB_PROVIDE_PATHS_COMPAT_LINUX");
				break;
			case (PKG_SHLIB_FLAGS_COMPAT_32 | PKG_SHLIB_FLAGS_COMPAT_LINUX):
				paths = pkg_config_get("SHLIB_PROVIDE_PATHS_COMPAT_LINUX_32");
				break;
			default:
				assert(0);
			}
			assert(paths != NULL);

			if (lstat(fpath, &st) != 0) {
				pkg_emit_errno("lstat() failed for", fpath);
				continue;
			}
			/* If the corresponding PATHS option isn't set (i.e. an empty ucl array)
			   don't do any filtering for backwards compatibility. */
			if (ucl_array_size(paths) == 0 || pkg_match_paths_list(paths, file->path)) {
				lstat(fpath, &st);
				if (S_ISREG(st.st_mode)) {
					pkg_addshlib_provided(pkg, provided, provided_flags);
				} else {
					vec_push(&maybe_provided, pkg_shlib_name_with_flags(provided, provided_flags));
				}
			} else {
				vec_push(&internal_provided, pkg_shlib_name_with_flags(provided, provided_flags));
			}
			free(provided);
		}
	}

	vec_foreach(maybe_provided, i) {
		vec_foreach(internal_provided, j) {
			if (STREQ(maybe_provided.d[i], internal_provided.d[j])) {
				pkg_addshlib_provided(pkg, maybe_provided.d[i], PKG_SHLIB_FLAGS_NONE);
				vec_remove_and_free(&internal_provided, j, free);
			}
		}
		vec_remove_and_free(&maybe_provided, i, free);
	}
	vec_free(&maybe_provided);
	/*
	 * Do not depend on libraries that a package provides itself
	 */
	pkg_cleanup_shlibs_required(pkg, &internal_provided);
	vec_free_and_free(&internal_provided, free);

	vec_foreach(pkg->shlibs_provided, i) {
		if (match_ucl_lists(pkg->shlibs_provided.d[i],
		    pkg_config_get("SHLIB_PROVIDE_IGNORE_GLOB"),
		    pkg_config_get("SHLIB_PROVIDE_IGNORE_REGEX"))) {
			pkg_debug(2,
			    "remove %s from provided shlibs for package %s as it "
			    "is matched by SHLIB_PROVIDE_IGNORE_GLOB/REGEX.",
			    pkg->shlibs_provided.d[i], pkg->name);
			vec_remove_and_free(&pkg->shlibs_provided, i, free);
			continue;
		}
	}

	/*
	 * if the package is not supposed to provide share libraries then
	 * drop the provided one
	 */
	if (pkg_kv_get(&pkg->annotations, "no_provide_shlib") != NULL) {
		vec_free_and_free(&pkg->shlibs_provided, free);
	}

	if (failures)
		goto cleanup;

	ret = EPKG_OK;

cleanup:
	ret = pkg_analyse_close();

	return (ret);
}
