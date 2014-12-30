/*-
 * Copyright (c) 2014 Landon Fuller <landon@landonf.org>
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

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include <mach/machine.h>
#include <mach-o/arch.h>
#include <mach-o/loader.h>

#include <ctype.h>
#include <limits.h>

#include <libmachista.h>
#include <bsd_compat.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

static const char * const system_dylib_prefixes[] = {
	"/System/",
	"/usr/lib",
	"/lib",
	NULL
 };

static int
analyse_macho(struct pkg *pkg, const char *fpath,
	      cpu_type_t cpu_type, macho_handle_t *macho_handle,
	      int (action)(void *, struct pkg *, const char *, const char *, bool),
	      void *actdata)
{
	const macho_t *macho = NULL;
	struct stat sb;
	int mret;

	/* We're only interested in the generic CPU type */
	cpu_type = cpu_type & ~CPU_ARCH_MASK;

	if (lstat(fpath, &sb) != 0)
		pkg_emit_errno("fstat() failed for", fpath);

	/* ignore empty files and non regular files */
	if (sb.st_size == 0 || !S_ISREG(sb.st_mode))
		return EPKG_END; /* Empty file or sym-link: no results */


	/* Try to parse the file */
	if ((mret = macho_parse_file(macho_handle, fpath, &macho)) != MACHO_SUCCESS) {
		if (mret != MACHO_EMAGIC && mret != MACHO_ERANGE) {
			pkg_emit_error("macho_parse_file() for %s failed: %s", fpath, macho_strerror(mret));
			return EPKG_FATAL; /* Empty file or sym-link: no results */
		}

		/* Not a Mach-O file; no results */
		return EPKG_END;
	}

	for (macho_arch_t *march = macho->mt_archs; march != NULL; march = march->next) {
		const NXArchInfo *ai;
		bool is_shlib = false;

		/* Determine the architecture name */
		ai = NXGetArchInfoFromCpuType(march->mat_cputype, march->mat_cpusubtype);
		if (ai == NULL) {
			pkg_emit_notice("Could not determine architecture type for cpu %d subtype %d", march->mat_cputype, march->mat_cpusubtype);
			continue;
		}

		/* Register non-relative libraries as provided. */
		// MACTODO: How to handle @rpath/@loader_path/etc?
		if (march->mat_install_name != NULL && march->mat_install_name[0] != '/') {
			// XXX MACTODO: To get things working, we're shoving library metadata into the library name;
			// these should instead be added as supported attributes of package shared library declarations.
			char *libname;
			asprintf(&libname, "%s.%s", march->mat_install_name, ai->name);
			pkg_addshlib_provided(pkg, libname);
			is_shlib = true;
		}

		/* Now find all dependencies */
		for (macho_loadcmd_t *cmd = march->mat_loadcmds; cmd != NULL; cmd = cmd->next) {
			/* Skip everything except for non-weak dylib references */
			if (cmd->mlt_type != LC_LOAD_DYLIB)
				continue;

			/* Prevent cyclic self-references. A valid dylib shouldn't include a
			 * LC_LOAD_DYLIB referencing itself, but there's nothing that would
			 * actually prevent it */
			if (strcmp(cmd->mlt_install_name, march->mat_install_name) == 0)
				continue;

			/* Skip non-resolvable library paths. */
			switch (cmd->mlt_install_name[0]) {
				case '/':
					break;
				case '@':
					// MACTODO: How to handle @rpath/@loader_path/etc?
					continue;
				default:
					continue;
			}

			// XXX MACTODO: To get things working, we're shoving library metadata into the library name;
			// these should instead be added as supported attributes of package shared library declarations.
			// XXX: This duplicates (and must be kept identical to) the libname construction above.
			char *libname;
			asprintf(&libname, "%s.%s", cmd->mlt_install_name, ai->name);

			action(actdata, pkg, fpath, libname, is_shlib);
		}
	}

	return EPKG_OK;
}

static int
add_dylibs_to_pkg(__unused void *actdata, struct pkg *pkg, const char *fpath,
		  const char *name, bool is_shlib)
{
	/* Skip references to system libraries */
	for (size_t i = 0; i < sizeof(system_dylib_prefixes) / sizeof(system_dylib_prefixes[0]); i++) {
		const char *prefix = system_dylib_prefixes[i];
		if (strncmp(name, prefix, strlen(prefix)) == 0)
			return EPKG_OK;
	}

	/* Record the library requirement. */
	pkg_addshlib_required(pkg, name);

	return EPKG_OK;
}

/**
 * Extract the major release number from an XNU kernel
 * version returned by uname().
 *
 * @param src A major.minor.revision version string, e.g., as returned from uname(3).
 * @param release On success, the parsed major version.
 */
static int
parse_major_release(const char *src, long long *release)
{
	int ret = EPKG_OK;
	char *parsed = NULL;
	const char *errstr;
	char *eos;

	parsed = strdup(src);
	eos = strchr(parsed, '.');
	if (eos == NULL) {
		pkg_emit_error("failed to parse major release version from %s", src);
		ret = EPKG_FATAL;
		goto cleanup;
	}

	*eos = '\0';
	*release = strtonum(parsed, 1, LONG_LONG_MAX, &errstr);
	if (errstr != NULL) {
		pkg_emit_error("failed to parse major release version from %s: %s", src, errstr);
		ret = EPKG_FATAL;
		goto cleanup;
	}

cleanup:
	free(parsed);
	return ret;
}

/**
 * Fetch the host's CPU type.
 *
 * @param result On success, the fetched CPU type.
 */
static int
host_cpu_type(cpu_type_t *result)
{
	size_t len;
	int resint;

	/* Fetch CPU type */
	len = sizeof(resint);
	if (sysctlbyname("hw.cputype", &resint, &len, NULL, 0) != 0) {
		pkg_emit_errno("sysctlbyname", "hw.cputype");
		return EPKG_FATAL;
	}

	*result = resint;
	return EPKG_OK;
}

/**
 * Fetch the OS name and major version.
 *
 * @param osname On success, the OS name (e.g. Darwin).
 * @param sz The maximum number of bytes to be written to osname.
 * @param major_version On success, the major version of the host.
 */
static int
host_os_info(char *osname, size_t sz, long long *major_version)
{
	struct utsname ut;

	/* Fetch OS info from uname() */
	if (uname(&ut) != 0) {
		pkg_emit_errno("uname", "&ut");
		return EPKG_FATAL;
	}

	/* Provide the OS name to the caller. */
	if (sz < strlen(ut.sysname) + 1) {
		pkg_emit_error("provided buffer is too small for os name: %s", strlen(ut.sysname));
		return EPKG_FATAL;
	}

	strlcpy(osname, ut.sysname, sz);

	/* Parse the major release version */
	return parse_major_release(ut.release, major_version);
}

int
pkg_analyse_files(struct pkgdb *db, struct pkg *pkg, const char *stage)
{
	macho_handle_t *macho_handle = NULL;
	struct pkg_file *file = NULL;
	bool failures = false;
	cpu_type_t cpu_type;
	char *fpath = NULL;
	int ret = EPKG_OK;

	/* Determine our system's CPU type */
	if ((ret = host_cpu_type(&cpu_type)) != EPKG_OK)
		goto cleanup;

	/* Create our mach-o handle */
	macho_handle = macho_create_handle();
	if (macho_handle == NULL) {
			pkg_emit_error("macho_create_handle() failed");
			ret = EPKG_FATAL;
			goto cleanup;
	}

	/* Evaluate all package files */
	while ((ret = pkg_files(pkg, &file)) == EPKG_OK) {
		if (stage != NULL)
			free(fpath);

		if (stage != NULL)
			asprintf(&fpath, "%s/%s", stage, file->path);
		else
			fpath = file->path;

		if (fpath == NULL) {
			pkg_emit_error("pkg_analyse_files(): path allocation failed");
			ret = EPKG_FATAL;
			goto cleanup;
		}

		ret = analyse_macho(pkg, fpath, cpu_type, macho_handle, add_dylibs_to_pkg, db);
		if (ret != EPKG_OK && ret != EPKG_END) {
			failures = true;
		}
	}

	if (ret != EPKG_OK)
		goto cleanup;

cleanup:
	macho_destroy_handle(macho_handle);

	if (stage != NULL)
		free(fpath);

	if (failures)
		ret = EPKG_FATAL;

	return (ret);
}

int
pkg_arch_to_legacy(const char *arch, char *dest, size_t sz)
{
	int i = 0;
	const NXArchInfo *ai;
	const char *arch_name;
	bool is64;

	bzero(dest, sz);
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

	/* Map the architecture name to its CPU type */
	ai = NXGetArchInfoFromName(arch + i);
	if (ai == NULL) {
		pkg_emit_error("could not find architecture info for %s", arch + i);
		return EPKG_FATAL;
	}

	/* Fetch the base architecture name */
	arch_name = macho_get_arch_name(ai->cputype & ~CPU_ARCH_ABI64);
	if (arch_name == NULL) {
		pkg_emit_error("macho_get_arch_name() failed for %x", ai->cputype);
		return EPKG_FATAL;
	}

	/* Determine word size */
	is64 = (ai->cputype & CPU_ARCH_ABI64) != 0;

	/* Emit the result */
	snprintf(dest + i, sz - (arch + i - dest), "%s:%s", arch_name, is64 ? "64" : "32");
	return EPKG_OK;
}

int
pkg_get_myarch_legacy(char *dest, size_t sz)
{
	char current[sz];
	int ret;

	if ((ret = pkg_get_myarch(current, sizeof(current))) != EPKG_OK)
		return ret;

	return pkg_arch_to_legacy(current, dest, sz);
}

int
pkg_get_myarch(char *dest, size_t sz)
{
	cpu_type_t cpu_type;
	const char *cpu_name = NULL;
	long long major_version;
	char os_name[BUFSIZ];
	char *spec = NULL;
	int ret = EPKG_OK;

	/* Fetch basic OS info */
	if ((ret = host_os_info(os_name, sizeof(os_name), &major_version)) != EPKG_OK)
		goto cleanup;

	/* Fetch host CPU type */
	if ((ret = host_cpu_type(&cpu_type)) != EPKG_OK)
		goto cleanup;

	/* Fetch the name for the base CPU family */
	cpu_name = macho_get_arch_name(cpu_type);

	/* Produce the result */
	asprintf(&spec, "%s:%lld:%s", os_name, major_version, cpu_name);
	if (spec == NULL) {
		pkg_emit_error("asprintf() failed to allocate output string");
		ret = EPKG_FATAL;
		goto cleanup;
	}

	strlcpy(dest, spec, sz);

cleanup:
	free(spec);
	return ret;
}
