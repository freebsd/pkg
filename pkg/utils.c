/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

#include <sys/param.h>

#include <libutil.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <pkg.h>

#include "pkgcli.h"

bool
query_yesno(const char *msg, ...)
{
	int c;
	bool r = false;
	va_list ap;

	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);


	c = getchar();
	if (c == 'y' || c == 'Y')
		r = true;
	else if (c == '\n' || c == EOF)
		return false;

	while((c = getchar()) != '\n' && c != EOF)
		continue;

	return r;
}

char *
absolutepath(const char *src, char *dest, size_t dest_len) {
	char * res;
	size_t res_len, res_size, len;;
	char pwd[MAXPATHLEN];
	const char *ptr = src;
	const char *next;
	const char *slash;

	len = strlen(src);

	if (len != 0 && src[0] != '/') {
		if (getcwd(pwd, sizeof(pwd)) == NULL)
			return NULL;

		res_len = strlen(pwd);
		res_size = res_len + 1 + len + 1;
		res = malloc(res_size);
		strlcpy(res, pwd, res_size);
	} else {
		res_size = (len > 0 ? len : 1) + 1;
		res = malloc(res_size);
		res_len = 0;
	}

	next = src;
	for (ptr = src; next != NULL ; ptr = next + 1) {
		next = strchr(ptr, '/');

		if (next != NULL)
			len = next - ptr;
		else
			len = strlen(ptr);

		switch(len) {
			case 2:
				if (ptr[0] == '.' && ptr[1] == '.') {
					slash = strrchr(res, '/');
					if (slash != NULL) {
						res_len = slash - res;
						res[res_len] = '\0';
					}
					continue;
				}
				break;
			case 1:
				if (ptr[0] == '.')
					continue;

				break;
			case 0:
				continue;
		}
		res[res_len++] = '/';
		strlcpy(res + res_len, ptr, res_size);
		res_len += len;
		res[res_len] = '\0';
	}

	if (res_len == 0)
		strlcpy(res, "/", res_size);

	strlcpy(dest, res, dest_len);
	free(res);

	return &dest[0];
}


int
print_info(struct pkg * const pkg, unsigned int opt)
{
	struct pkg_dep *dep = NULL;
	struct pkg_file *file = NULL;
	struct pkg_category *cat = NULL;
	struct pkg_license *lic = NULL;
	struct pkg_option *option = NULL;
	struct pkg_shlib *shlib = NULL;
	bool multirepos_enabled = false;
	char *m;
	char size[7];
	const char *name, *version, *prefix, *origin, *reponame, *repourl;
	const char *maintainer, *www, *comment, *desc;
	int64_t flatsize, newflatsize, newpkgsize;
	lic_t licenselogic;

	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multirepos_enabled);

	pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version, PKG_PREFIX, &prefix,
	    PKG_ORIGIN, &origin, PKG_REPONAME, &reponame, PKG_REPOURL, &repourl,
	    PKG_MAINTAINER, &maintainer, PKG_WWW, &www, PKG_COMMENT, &comment,
	    PKG_DESC, &desc, PKG_FLATSIZE, &flatsize, PKG_NEW_FLATSIZE, &newflatsize,
		PKG_NEW_PKGSIZE, &newpkgsize, PKG_LICENSE_LOGIC, &licenselogic);

	if (opt & INFO_RAW) {
		pkg_emit_manifest(pkg, &m);
		printf("%s\n", m);
		free(m);
	} else if (opt & INFO_FULL) {
		printf("%-15s: %s\n", "Name", name);
		printf("%-15s: %s\n", "Version", version);
		printf("%-15s: %s\n", "Origin", origin);
		printf("%-15s: %s\n", "Prefix", prefix);

		if ((pkg_type(pkg) == PKG_REMOTE) && multirepos_enabled)
			printf("%-15s: %s [%s]\n", "Repository", reponame, repourl);

                if (!pkg_list_is_empty(pkg, PKG_CATEGORIES)) {
                        printf("%-15s:", "Categories");
                        while (pkg_categories(pkg, &cat) == EPKG_OK)
                                printf(" %s", pkg_category_name(cat));
                        printf("\n");
                }

		if (!pkg_list_is_empty(pkg, PKG_LICENSES)) {
			printf("%-15s:", "Licenses");
			while (pkg_licenses(pkg, &lic) == EPKG_OK) {
				printf(" %s", pkg_license_name(lic));
				if (licenselogic != 1)
					printf(" %c", licenselogic);
				else
					printf(" ");
			}
			printf("\b \n");
		}

		printf("%-15s: %s\n", "Maintainer", maintainer);
		printf("%-15s: %s\n", "WWW", www);
		printf("%-15s: %s\n", "Comment", comment);

                if (!pkg_list_is_empty(pkg, PKG_OPTIONS)) {
                        printf("%-15s: \n", "Options");
                        while (pkg_options(pkg, &option) == EPKG_OK)
                                printf("\t%s: %s\n", pkg_option_opt(option), pkg_option_value(option));
                }

		if (!pkg_list_is_empty(pkg, PKG_SHLIBS)) {
			printf("%-15s:", "SharedLibraries");
			while (pkg_shlibs(pkg, &shlib) == EPKG_OK)
				printf(" %s", pkg_shlib_name(shlib));
			printf("\n");
		}

		if (pkg_type(pkg) == PKG_INSTALLED || pkg_type(pkg) == PKG_FILE) {
			humanize_number(size, sizeof(size), flatsize, "B", HN_AUTOSCALE, 0);
			printf("%-15s: %s\n", "Flat size", size);
		} else {
			humanize_number(size, sizeof(size), newflatsize, "B", HN_AUTOSCALE, 0);
			printf("%-15s: %s\n", "Flat size", size);
			humanize_number(size, sizeof(size), newpkgsize, "B", HN_AUTOSCALE, 0);
			printf("%-15s: %s\n", "Pkg size", size);
		}

		printf("%-15s: \n%s\n", "Description", desc);
		printf("\n");
	} else if (opt & INFO_PRINT_DEP) {
		if (!(opt & INFO_QUIET))
			printf("%s-%s depends on:\n", name, version);

                while (pkg_deps(pkg, &dep) == EPKG_OK) {
                        printf("%s-%s\n", pkg_dep_get(dep, PKG_DEP_NAME), pkg_dep_get(dep, PKG_DEP_VERSION));
                }

                if (!(opt & INFO_QUIET))
                        printf("\n");
	} else if (opt & INFO_PRINT_RDEP) {
		if (!(opt & INFO_QUIET))
			printf("%s-%s is required by:\n", name, version);

                while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
                        printf("%s-%s\n", pkg_dep_get(dep, PKG_DEP_NAME), pkg_dep_get(dep, PKG_DEP_VERSION));
                }

                if (!(opt & INFO_QUIET))
                        printf("\n");
	} else if (opt & INFO_LIST_FILES) {
		if (!(opt & INFO_QUIET))
			printf("%s-%s owns the following files:\n", name, version);

                while (pkg_files(pkg, &file) == EPKG_OK) {
                        printf("%s\n", pkg_file_get(file, PKG_FILE_PATH));
                }

                if (!(opt & INFO_QUIET))
                        printf("\n");
	} else if (opt & INFO_LIST_SHLIBS) {
		if (!(opt & INFO_QUIET))
			printf("%s-%s uses the following shared libraries:\n", name, version);

                while (pkg_shlibs(pkg, &shlib) == EPKG_OK) {
                        printf("%s\n", pkg_shlib_name(shlib));
                }

                if (!(opt & INFO_QUIET))
                        printf("\n");
        } else if (opt & INFO_SIZE) {
		if (pkg_type(pkg) == PKG_INSTALLED) {
			humanize_number(size, sizeof(size), flatsize, "B", HN_AUTOSCALE, 0);
			printf("%s-%s size is: %s\n", name, version, size);
		} else {
			humanize_number(size, sizeof(size), newflatsize, "B", HN_AUTOSCALE, 0);
			printf("%s-%s flat size is: %s\n", name, version, size);
			humanize_number(size, sizeof(size), newpkgsize, "B", HN_AUTOSCALE, 0);
			printf("%s-%s package size is: %s\n", name, version, size);
		}
        } else if (opt & INFO_ORIGIN) {
                if (opt & INFO_QUIET)
                        printf("%s\n", origin);
                else
                        printf("%s-%s: %s\n", name, version, origin);
        } else if (opt & INFO_PREFIX) {
                if (opt & INFO_QUIET)
                        printf("%s\n", prefix);
                else
                        printf("%s-%s: %s\n", name, version, prefix);
        } else {
                if (opt & INFO_QUIET)
                        printf("%s-%s\n", name, version);
                else {
			if ((pkg_type(pkg) == PKG_REMOTE) && multirepos_enabled)
				printf("%s-%s [repository: %s]: %s\n", name, version, reponame, comment);
			else
				printf("%s-%s: %s\n", name, version, comment);
		}
        }

        return (0);
}
