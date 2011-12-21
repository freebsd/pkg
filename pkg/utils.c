#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <string.h>
#include <pkg.h>

#include "utils.h"

bool
query_yesno(const char *msg)
{
        int c;
	bool r = false;

        printf("%s", msg);

        c = getchar();
        if (c == 'y' || c == 'Y')
                r = true;
        else if (c == '\n' || c == EOF)
                return false;

        while((c = getchar()) != '\n' && c != EOF)
                continue;

        return r;
}

int
print_info(struct pkg * const pkg, unsigned int opt)
{
	struct pkg_dep *dep = NULL;
	struct pkg_file *file = NULL;
	struct pkg_category *cat = NULL;
	struct pkg_license *lic = NULL;
	struct pkg_option *option = NULL;
	bool multirepos_enabled = false;
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

        if (opt & INFO_FULL) {
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
			if (multirepos_enabled)
				printf("%s-%s [repository: %s]: %s\n", name, version, reponame, comment);
			else
				printf("%s-%s: %s\n", name, version, comment);
		}
        }

        return (0);
}
