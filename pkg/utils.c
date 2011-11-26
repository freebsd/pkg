#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <string.h>
#include <pkg.h>

#include "utils.h"

int
query_yesno(const char *msg)
{
        int c, r = 0;

        printf("%s", msg);

        c = getchar();
        if (c == 'y' || c == 'Y')
                r = 1;
        else if (c == '\n' || c == EOF)
                return 0;

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
        char size[7];

        if (opt & INFO_FULL) {
                printf("%-15s: %s\n", "Name", pkg_get(pkg, PKG_NAME));
                printf("%-15s: %s\n", "Version", pkg_get(pkg, PKG_VERSION));
                printf("%-15s: %s\n", "Origin", pkg_get(pkg, PKG_ORIGIN));
                printf("%-15s: %s\n", "Prefix", pkg_get(pkg, PKG_PREFIX));

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
                                if (pkg_licenselogic(pkg) != 1)
                                        printf(" %c", pkg_licenselogic(pkg));
                                else
                                        printf(" ");
                        }
                        printf("\b \n");
                }
                printf("%-15s: %s\n", "Maintainer", pkg_get(pkg, PKG_MAINTAINER));
                printf("%-15s: %s\n", "WWW", pkg_get(pkg, PKG_WWW));
                printf("%-15s: %s\n", "Comment", pkg_get(pkg, PKG_COMMENT));

                if (!pkg_list_is_empty(pkg, PKG_OPTIONS)) {
                        printf("%-15s: \n", "Options");
                        while (pkg_options(pkg, &option) == EPKG_OK)
                                printf("\t%s: %s\n", pkg_option_opt(option), pkg_option_value(option));
                }

		if (pkg_type(pkg) == PKG_INSTALLED || pkg_type(pkg) == PKG_FILE) {
			humanize_number(size, sizeof(size), pkg_flatsize(pkg), "B", HN_AUTOSCALE, 0);
			printf("%-15s: %s\n", "Flat size", size);
		} else {
			humanize_number(size, sizeof(size), pkg_new_flatsize(pkg), "B", HN_AUTOSCALE, 0);
			printf("%-15s: %s\n", "Flat size", size);
			humanize_number(size, sizeof(size), pkg_new_pkgsize(pkg), "B", HN_AUTOSCALE, 0);
			printf("%-15s: %s\n", "Pkg size", size);
		}

                printf("%-15s: \n%s\n", "Description", pkg_get(pkg, PKG_DESC));
                printf("\n");
        } else if (opt & INFO_PRINT_DEP) {
                if (!(opt & INFO_QUIET))
                        printf("%s-%s depends on:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

                while (pkg_deps(pkg, &dep) == EPKG_OK) {
                        printf("%s-%s\n", pkg_dep_name(dep), pkg_dep_version(dep));
                }

                if (!(opt & INFO_QUIET))
                        printf("\n");
        } else if (opt & INFO_PRINT_RDEP) {
                if (!(opt & INFO_QUIET))
                        printf("%s-%s is required by:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

                while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
                        printf("%s-%s\n", pkg_dep_name(dep), pkg_dep_version(dep));
                }

                if (!(opt & INFO_QUIET))
                        printf("\n");
        } else if (opt & INFO_LIST_FILES) {
                if (!(opt & INFO_QUIET))
                        printf("%s-%s owns the following files:\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

                while (pkg_files(pkg, &file) == EPKG_OK) {
                        printf("%s\n", pkg_file_path(file));
                }

                if (!(opt & INFO_QUIET))
                        printf("\n");
        } else if (opt & INFO_SIZE) {
		if (pkg_type(pkg) == PKG_INSTALLED) {
			humanize_number(size, sizeof(size), pkg_flatsize(pkg), "B", HN_AUTOSCALE, 0);
			printf("%s-%s size is: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), size);
		} else {
			humanize_number(size, sizeof(size), pkg_new_flatsize(pkg), "B", HN_AUTOSCALE, 0);
			printf("%s-%s flat size is: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), size);
			humanize_number(size, sizeof(size), pkg_new_pkgsize(pkg), "B", HN_AUTOSCALE, 0);
			printf("%s-%s package size is: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), size);
		}
        } else if (opt & INFO_ORIGIN) {
                if (opt & INFO_QUIET)
                        printf("%s\n", pkg_get(pkg, PKG_ORIGIN));
                else
                        printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_ORIGIN));
        } else if (opt & INFO_PREFIX) {
                if (opt & INFO_QUIET)
                        printf("%s\n", pkg_get(pkg, PKG_PREFIX));
                else
                        printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_PREFIX));
        } else {
                if (opt & INFO_QUIET)
                        printf("%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
                else
                        printf("%s-%s: %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_COMMENT));
        }

        return (0);
}
