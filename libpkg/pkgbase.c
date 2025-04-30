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
#include "private/utils.h"
#include "xmalloc.h"

struct pkgbase {
	charv_t system_shlibs;
	/*
	 * unused yet but will be in the future when we will start using
	 * provides/requires in pkgbase
	 */
	charv_t provides;
	bool ignore_compat32;
};

static int
scan_dir_for_shlibs(charv_t *shlib_list, const char *dir,
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
	size_t cnt = 0;
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
		vec_push(shlib_list, full);
		cnt++;
	}
	if (cnt == 0)
		errno = ENOENT;

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
scan_system_shlibs(charv_t *system_shlibs, const char *rootdir)
{
	int r = EPKG_OK;
	for (int i = 0; i < NELEM(system_shlib_table); i++) {
		char *dir;
		if (rootdir != NULL) {
			xasprintf(&dir, "%s%s", rootdir, system_shlib_table[i].dir);
		} else {
			dir = xstrdup(system_shlib_table[i].dir);
		}
		errno = 0;
		int ret = scan_dir_for_shlibs(system_shlibs, dir, system_shlib_table[i].flags);
		free(dir);
		if (errno == ENOENT)
			r = EPKG_NOCOMPAT32;
		if (ret != EPKG_OK) {
			return (ret);
		}
	}
	if (system_shlibd->d)
		qsort(system_shlibs->d, system_shlibs->len, sizeof(char *), char_cmp);

	return (r);
}

struct pkgbase *
pkgbase_new(struct pkgdb *db)
{
	struct pkgbase *pb = xcalloc(1, sizeof(*pb));

	if (!pkgdb_file_exists(db, "/usr/bin/uname"))
		if (scan_system_shlibs(&pb->system_shlibs, ctx.pkg_rootdir) == EPKG_NOCOMPAT32)
			pb->ignore_compat32 = true;

	return (pb);
}

void
pkgbase_free(struct pkgbase *pb)
{
	if (pb == NULL)
		return;
	vec_free_and_free(&pb->system_shlibs, free);
	vec_free_and_free(&pb->provides, free);
	free(pb);
}

bool
pkgbase_provide_shlib(struct pkgbase *pb, const char *shlib)
{
	if (pb->ignore_compat32 && str_ends_with(shlib, ":32"))
		return (true);
	return (charv_search(&pb->system_shlibs, shlib) != NULL);
}

bool
pkgbase_provide(struct pkgbase *pb, const char *provide)
{
	return (charv_search(&pb->provides, provide) != NULL);
}
