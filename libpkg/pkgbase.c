/*-
 * Copyright (c) 1998 John D. Polstra
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2024 The FreeBSD Foundation
 *
 * This software was developed in part by Isaac Freund <ifreund@freebsdfoundation.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#include "pkg.h"
#include "pkghash.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "xmalloc.h"

struct pkgbase {
	struct pkghash *system_shlibs;
	/*
	 * unused yet but will be in the future when we will start using
	 * provides/requires in pkgbase
	 */
	struct pkghash *provides;
};

static int
scan_dir_for_shlibs(pkghash **shlib_list, const char *dir,
    enum pkg_shlib_flags flags)
{
	DIR *dirp= opendir(dir);
	if (dirp == NULL) {
		if (errno == ENOENT) {
			return (EPKG_OK);
		}
		pkg_errno("Failed to open '%s' to scan for shared libraries", dir);
		return (EPKG_FATAL);
	}

	struct dirent *dp;
	while ((dp = readdir(dirp)) != NULL) {
		/* Only regular files and sym-links. On some
		   filesystems d_type is not set, on these the d_type
		   field will be DT_UNKNOWN. */
		if (dp->d_type != DT_REG && dp->d_type != DT_LNK &&
		    dp->d_type != DT_UNKNOWN)
			continue;

		int len = strlen(dp->d_name);
		/* Name can't be shorter than "libx.so" */
		if (len < 7 || strncmp(dp->d_name, "lib", 3) != 0)
			continue;

		const char *vers = dp->d_name + len;
		while (vers > dp->d_name &&
		       (isdigit(*(vers-1)) || *(vers-1) == '.'))
			vers--;
		if (vers == dp->d_name + len) {
			if (strncmp(vers - 3, ".so", 3) != 0)
				continue;
		} else if (vers < dp->d_name + 3 ||
		    strncmp(vers - 3, ".so.", 4) != 0)
			continue;

		/* We have a valid shared library name. */
		char *full = pkg_shlib_name_with_flags(dp->d_name, flags);
		pkghash_safe_add(*shlib_list, full, NULL, NULL);
		free(full);
	}

	closedir(dirp);

	return (EPKG_OK);
}

static struct {
	const char *dir;
	enum pkg_shlib_flags flags;
} system_shlib_table[] = {
	{"/lib", PKG_SHLIB_FLAGS_NONE },
	{"/usr/lib", PKG_SHLIB_FLAGS_NONE },
	{"/usr/lib32", PKG_SHLIB_FLAGS_COMPAT_32 },
};

int
scan_system_shlibs(pkghash **system_shlibs, const char *rootdir)
{
	for (int i = 0; i < NELEM(system_shlib_table); i++) {
		char *dir;
		if (rootdir != NULL) {
			xasprintf(&dir, "%s%s", rootdir, system_shlib_table[i].dir);
		} else {
			dir = xstrdup(system_shlib_table[i].dir);
		}
		int ret = scan_dir_for_shlibs(system_shlibs, dir, system_shlib_table[i].flags);
		free(dir);
		if (ret != EPKG_OK) {
			return (ret);
		}
	}

	return (EPKG_OK);
}

struct pkgbase *
pkgbase_new(struct pkgdb *db)
{
	struct pkgbase *pb = xcalloc(1, sizeof(*pb));

	if (!pkgdb_file_exists(db, "/usr/bin/uname"))
		scan_system_shlibs(&pb->system_shlibs, ctx.pkg_rootdir);

	return (pb);
}

void
pkgbase_free(struct pkgbase *pb)
{
	if (pb == NULL)
		return;
	pkghash_destroy(pb->system_shlibs);
	pkghash_destroy(pb->provides);
	free(pb);
}

bool
pkgbase_provide_shlib(struct pkgbase *pb, const char *shlib)
{
	return (pkghash_get(pb->system_shlibs, shlib) != NULL);
}

bool
pkgbase_provide(struct pkgbase *pb, const char *provide)
{
	return (pkghash_get(pb->provides, provide) != NULL);
}
