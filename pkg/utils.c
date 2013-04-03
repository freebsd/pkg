/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libutil.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <paths.h>
#include <pkg.h>

#include "pkgcli.h"

bool
query_yesno(const char *msg, ...)
{
	int	 c;
	bool	 r = false;
	va_list	 ap;
	int	 tty_fd;
	FILE	*tty;

	tty_fd = open(_PATH_TTY, O_RDWR|O_TTY_INIT);
	if (tty_fd == -1)
		return (r);		/* No ctty -- return the
					 * default answer */

	tty = fdopen(tty_fd, "r+");

	va_start(ap, msg);
	vfprintf(tty, msg, ap);
	va_end(ap);

	c = getc(tty);
	if (c == 'y' || c == 'Y')
		r = true;
	else if (c == '\n' || c == EOF)
		return false;

	while ((c = getc(tty)) != '\n' && c != EOF)
		continue;

	fclose(tty);

	return r;
}

char *
absolutepath(const char *src, char *dest, size_t dest_len) {
	char * res;
	size_t res_len, res_size, len;
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

		switch (len) {
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

/* what the pkg needs to load in order to display the requested info */
int
info_flags(unsigned int opt)
{
	int flags = PKG_LOAD_BASIC;

	if (opt & INFO_CATEGORIES)
		flags |= PKG_LOAD_CATEGORIES;
	if (opt & INFO_LICENSES)
		flags |= PKG_LOAD_LICENSES;
	if (opt & INFO_OPTIONS)
		flags |= PKG_LOAD_OPTIONS;
	if (opt & INFO_SHLIBS_REQUIRED)
		flags |= PKG_LOAD_SHLIBS_REQUIRED;
	if (opt & INFO_SHLIBS_PROVIDED)
		flags |= PKG_LOAD_SHLIBS_PROVIDED;
	if (opt & INFO_DEPS)
		flags |= PKG_LOAD_DEPS;
	if (opt & INFO_RDEPS)
		flags |= PKG_LOAD_RDEPS;
	if (opt & INFO_FILES)
		flags |= PKG_LOAD_FILES;
	if (opt & INFO_DIRS)
		flags |= PKG_LOAD_DIRS;
	if (opt & INFO_USERS)
		flags |= PKG_LOAD_USERS;
	if (opt & INFO_GROUPS)
		flags |= PKG_LOAD_GROUPS;
	if (opt & INFO_RAW) {
		flags |= PKG_LOAD_CATEGORIES      |
			 PKG_LOAD_LICENSES        |
			 PKG_LOAD_OPTIONS         |
			 PKG_LOAD_SHLIBS_REQUIRED |
			 PKG_LOAD_DEPS            |
			 PKG_LOAD_FILES           |
			 PKG_LOAD_DIRS            |
			 PKG_LOAD_USERS           |
			 PKG_LOAD_GROUPS          |
			 PKG_LOAD_SCRIPTS;
	}

	return flags;
}

void
print_info(struct pkg * const pkg, unsigned int options)
{
	struct pkg_category *cat    = NULL;
	struct pkg_dep	    *dep    = NULL;
	struct pkg_dir	    *dir    = NULL;
	struct pkg_file	    *file   = NULL;
	struct pkg_group    *group  = NULL;
	struct pkg_license  *lic    = NULL;
	struct pkg_option   *option = NULL;
	struct pkg_shlib    *shlib  = NULL;
	struct pkg_user	    *user   = NULL;
	bool multirepos_enabled = false;
	bool print_tag = false;
	bool show_locks = false;
	char size[7];
	const char *name, *version, *prefix, *origin, *reponame, *repourl;
	const char *maintainer, *www, *comment, *desc, *message, *arch;
	const char *repopath;
	const char *tab;
	unsigned opt;
	int64_t flatsize, newflatsize, newpkgsize;
	lic_t licenselogic;
	bool locked;
	int cout = 0;		/* Number of characters output */
	int info_num;		/* Number of different data items to print */

	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multirepos_enabled);

	pkg_get(pkg,
		PKG_NAME,          &name,
		PKG_VERSION,       &version,
		PKG_PREFIX,        &prefix,
		PKG_ORIGIN,        &origin,
		PKG_REPONAME,      &reponame,
		PKG_REPOURL,       &repourl,
		PKG_MAINTAINER,    &maintainer,
		PKG_WWW,           &www,
		PKG_COMMENT,       &comment,
		PKG_DESC,          &desc,
		PKG_FLATSIZE,      &flatsize,
		PKG_NEW_FLATSIZE,  &newflatsize,
		PKG_NEW_PKGSIZE,   &newpkgsize,
		PKG_LICENSE_LOGIC, &licenselogic,
		PKG_MESSAGE,       &message,
		PKG_ARCH,	   &arch,
		PKG_REPOPATH,	   &repopath,
		PKG_LOCKED,	   &locked);

	if (!multirepos_enabled)
		pkg_config_string(PKG_CONFIG_REPO, &repourl);

	if (options & INFO_RAW) { /* Not for remote packages */
		if (pkg_type(pkg) != PKG_REMOTE)
			pkg_emit_manifest_file(pkg, stdout, false, NULL);
		return;
	}

	/* Show locking status when requested to display it and the
	   package is locally installed */
	if (pkg_type(pkg) == PKG_INSTALLED && (options & INFO_LOCKED) != 0)
		show_locks = true;

	if (!quiet) {
		/* Print a tag-line identifying the package -- either
		   NAMEVER, ORIGIN or NAME (in that order of
		   preference).  This may be the only output from this
		   function */

		if (options & INFO_TAG_NAMEVER)
			cout = printf("%s-%s", name, version);
		else if (options & INFO_TAG_ORIGIN)
			cout = printf("%s", origin);
		else if (options & INFO_TAG_NAME)
			cout = printf("%s", name);
	}

	/* Don't display a tab if quiet, retains compatibility. */
	tab = quiet ? "" : "\t";

	/* If we printed a tag, and there are no other items to print,
	   then just return now. If there's only one single-line item
	   to print, show it at column 32 on the same line. If there's
	   one multi-line item to print, start a new line. If there is
	   more than one item to print per pkg, use 'key : value'
	   style to show on a new line.  */

	info_num = 0;
	for (opt = 0x1U; opt <= INFO_LASTFIELD; opt <<= 1) 
		if ((opt & options) != 0)
			info_num++;

	if (info_num == 0 && cout > 0) {
		printf("\n");
		return;
	}

	if (info_num == 1) {
		/* Only one item to print */
		print_tag = false;
		if (!quiet) {
			if (options & INFO_MULTILINE)
				printf(":\n");
			else {
				if (cout < 31)
					cout = 31 - cout;
				else
					cout = 1;
				printf("%*s", cout, " ");
			}
		}
	} else {
		/* Several items to print */
		print_tag = true;
		if (!quiet)
			printf("\n");
	}

	for (opt = 0x1; opt <= INFO_LASTFIELD; opt <<= 1) {
		if ((opt & options) == 0)
			continue;

		switch (opt) {
		case INFO_NAME:
			if (print_tag)
				printf("%-15s: ", "Name");
			printf("%s\n", name);
			break;
		case INFO_VERSION:
			if (print_tag)
				printf("%-15s: ", "Version");
			printf("%s\n", version);
			break;
		case INFO_ORIGIN:
			if (print_tag)
				printf("%-15s: ", "Origin");
			printf("%s\n", origin);
			break;
		case INFO_PREFIX:
			if (print_tag)
				printf("%-15s: ", "Prefix");
			printf("%s\n", prefix);
			break;
		case INFO_REPOSITORY:
			if (pkg_type(pkg) == PKG_REMOTE &&
			    repourl != NULL && repourl[0] != '\0') {
				if (print_tag)
					printf("%-15s: ", "Repository");
				printf("%s [%s]\n", reponame, repourl);
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_CATEGORIES:
			if (pkg_list_count(pkg, PKG_CATEGORIES) > 0) {
				if (print_tag)
					printf("%-15s: ", "Categories");
				if (pkg_categories(pkg, &cat) == EPKG_OK)
					printf("%s", pkg_category_name(cat));
				while (pkg_categories(pkg, &cat) == EPKG_OK)
					printf(" %s", pkg_category_name(cat));
				printf("\n");
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_LICENSES:
			if (pkg_list_count(pkg, PKG_LICENSES) > 0) {
				if (print_tag)
					printf("%-15s: ", "Licenses");
				if (pkg_licenses(pkg, &lic) == EPKG_OK)
					printf("%s", pkg_license_name(lic));
				while (pkg_licenses(pkg, &lic) == EPKG_OK) {
					if (licenselogic != 1)
						printf(" %c", licenselogic);
					printf(" %s", pkg_license_name(lic));
				}
				printf("\n");				
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_MAINTAINER:
			if (print_tag)
				printf("%-15s: ", "Maintainer");
			printf("%s\n", maintainer);
			break;
		case INFO_WWW:	
			if (print_tag)
				printf("%-15s: ", "WWW");
			printf("%s\n", www);
			break;
		case INFO_COMMENT:
			if (print_tag)
				printf("%-15s: ", "Comment");
			printf("%s\n", comment);
			break;
		case INFO_OPTIONS:
			if (pkg_list_count(pkg, PKG_OPTIONS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Options");
				while (pkg_options(pkg, &option) == EPKG_OK)
					printf("%s%-15s: %s\n",
					       tab,
					       pkg_option_opt(option),
					       pkg_option_value(option));
			}
			break;
		case INFO_SHLIBS_REQUIRED:
			if (pkg_list_count(pkg, PKG_SHLIBS_REQUIRED) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Shared Libs required");
				while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK)
					printf("%s%s\n", tab, pkg_shlib_name(shlib));
			}
			break;
		case INFO_SHLIBS_PROVIDED:
			if (pkg_list_count(pkg, PKG_SHLIBS_PROVIDED) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Shared Libs provided");
				while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK)
					printf("%s%s\n", tab, pkg_shlib_name(shlib));
			}
			break;
		case INFO_FLATSIZE:
			if (pkg_type(pkg) == PKG_INSTALLED ||
			    pkg_type(pkg) == PKG_FILE)
				humanize_number(size, sizeof(size),
						flatsize,"B",
						HN_AUTOSCALE, 0);
			else
				humanize_number(size, sizeof(size),
						newflatsize,"B",
						HN_AUTOSCALE, 0);

			if (print_tag)
				printf("%-15s: ", "Flat size");
			printf("%s\n", size);
			break;
		case INFO_PKGSIZE: /* Remote pkgs only */
			if (pkg_type(pkg) == PKG_REMOTE) {
				humanize_number(size, sizeof(size),
						newpkgsize,"B",
						HN_AUTOSCALE, 0);
				if (print_tag)
					printf("%-15s: ", "Pkg size");
				printf("%s\n", size);
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_DESCR:
			if (print_tag)
				printf("%-15s:\n", "Description");
			printf("%s\n", desc);
			break;
		case INFO_MESSAGE:
			if (message) {
				if (print_tag)
					printf("%-15s:\n", "Message");
				printf("%s\n", message);
			}
			break;
		case INFO_DEPS:
			if (pkg_list_count(pkg, PKG_DEPS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Depends on");
				while (pkg_deps(pkg, &dep) == EPKG_OK) {
					printf("%s%s-%s",
					       tab,
					       pkg_dep_name(dep),
					       pkg_dep_version(dep));
					if (show_locks && pkg_dep_is_locked(dep))
						printf(" (*)");
					printf("\n");
				}
			}
			break;
		case INFO_RDEPS:
			if (pkg_list_count(pkg, PKG_RDEPS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Required by");
				while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
					printf("%s%s-%s",
					       tab,
					       pkg_dep_name(dep),
					       pkg_dep_version(dep));
					if (show_locks && pkg_dep_is_locked(dep))
						printf(" (*)");
					printf("\n");
				}
			}
			break;
		case INFO_FILES: /* Installed pkgs only */
			if (pkg_type(pkg) != PKG_REMOTE &&
			    pkg_list_count(pkg, PKG_FILES) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Files");
				while (pkg_files(pkg, &file) == EPKG_OK)
					printf("%s%s\n",
					       tab,
					       pkg_file_path(file));
			}
			break;
		case INFO_DIRS:	/* Installed pkgs only */
			if (pkg_type(pkg) != PKG_REMOTE &&
			    pkg_list_count(pkg, PKG_DIRS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Directories");
				while (pkg_dirs(pkg, &dir) == EPKG_OK)
					printf("%s%s\n",
					       tab,
					       pkg_dir_path(dir));
			}
			break;
		case INFO_USERS: /* Installed pkgs only */
			if (pkg_type(pkg) != PKG_REMOTE &&
			    pkg_list_count(pkg, PKG_USERS) > 0) {
				if (print_tag)
					printf("%-15s: ", "Users");
				if (pkg_users(pkg, &user) == EPKG_OK)
					printf("%s", pkg_user_name(user));
				while (pkg_users(pkg, &user) == EPKG_OK)
					printf(" %s", pkg_user_name(user));
				printf("\n");
			}
			break;
		case INFO_GROUPS: /* Installed pkgs only */
			if (pkg_type(pkg) != PKG_REMOTE &&
			    pkg_list_count(pkg, PKG_GROUPS) > 0) {
				if (print_tag)
					printf("%-15s: ", "Groups");
				if (pkg_groups(pkg, &group) == EPKG_OK)
					printf("%s", pkg_group_name(group));
				while (pkg_groups(pkg, &group) == EPKG_OK)
					printf(" %s", pkg_group_name(group));
				printf("\n");
			}
			break;
		case INFO_ARCH:
			if (print_tag)
				printf("%-15s: ", "Architecture");
			printf("%s\n", arch);
			break;
		case INFO_REPOURL:
			if (pkg_type(pkg) == PKG_REMOTE &&
			    repourl != NULL && repourl[0] != '\0') {
				if (print_tag)
					printf("%-15s: ", "Pkg URL");
				if (repourl[strlen(repourl) -1] == '/')
					printf("%s%s\n", repourl, repopath);
				else
					printf("%s/%s\n", repourl, repopath);
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_LOCKED:
			if (print_tag)
				printf("%-15s: ", "Locked");
			printf("%s\n", locked ? "yes" : "no");
			break;
		}
	}
}

void
print_jobs_summary(struct pkg_jobs *jobs, const char *msg, ...)
{
	struct pkg *pkg = NULL;
	char path[MAXPATHLEN];
	struct stat st;
	const char *name, *version, *newversion, *pkgrepopath, *cachedir;
	int64_t dlsize, oldsize, newsize;
	int64_t flatsize, newflatsize, pkgsize;
	bool locked;
	char size[7];
	va_list ap;
	pkg_jobs_t type;

	type = pkg_jobs_type(jobs);

	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);

	dlsize = oldsize = newsize = 0;
	flatsize = newflatsize = pkgsize = 0;
	name = version = newversion = NULL;
	
	pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir);

	while (pkg_jobs(jobs, &pkg) == EPKG_OK) {
		pkg_get(pkg, PKG_NEWVERSION, &newversion, PKG_NAME, &name,
		    PKG_VERSION, &version, PKG_FLATSIZE, &flatsize,
		    PKG_NEW_FLATSIZE, &newflatsize, PKG_NEW_PKGSIZE, &pkgsize,
		    PKG_REPOPATH, &pkgrepopath, PKG_LOCKED, &locked);

		if (locked) {
			printf("\tPackage %s-%s is locked ",
			       name, version);
			switch (type) {
			case PKG_JOBS_INSTALL:
			case PKG_JOBS_UPGRADE:
				/* If it's a new install, then it
				 * cannot have been locked yet. */
				if (newversion != NULL) {
					switch(pkg_version_cmp(version, newversion)) {
					case -1:
						printf("and may not be upgraded to version %s\n", newversion);
						break;
					case 0:
						printf("and may not be reinstalled\n");
						break;
					case 1:
						printf("and may not be downgraded to version %s\n", newversion);
						break;
					}
					continue;
				} 
				break;
			case PKG_JOBS_DEINSTALL:
			case PKG_JOBS_AUTOREMOVE:
				printf("and may not be deinstalled\n");
				continue;
				break;
			case PKG_JOBS_FETCH:
				printf("but a new package can still be fetched\n");
				break;
			}

		}

		switch (type) {
		case PKG_JOBS_INSTALL:
		case PKG_JOBS_UPGRADE:
			snprintf(path, MAXPATHLEN, "%s/%s", cachedir, pkgrepopath);
			if (stat(path, &st) == -1 || pkgsize != st.st_size)
				/* file looks corrupted (wrong size), assume a checksum mismatch will
				   occur later and the file will be fetched from remote again */
				dlsize += pkgsize;

			if (newversion != NULL) {
				switch (pkg_version_cmp(version, newversion)) {
				case 1:
					printf("\tDowngrading %s: %s -> %s\n", name, version, newversion);
					break;
				case 0:
					printf("\tReinstalling %s-%s\n", name, version);
					break;
				case -1:
					printf("\tUpgrading %s: %s -> %s\n", name, version, newversion);
					break;
				}
				oldsize += flatsize;
				newsize += newflatsize;
			} else {
				newsize += flatsize;
				printf("\tInstalling %s: %s\n", name, version);
			}
			break;
		case PKG_JOBS_DEINSTALL:
		case PKG_JOBS_AUTOREMOVE:
			oldsize += flatsize;
			newsize += newflatsize;
			
			printf("\t%s-%s\n", name, version);
			break;
		case PKG_JOBS_FETCH:
			dlsize += pkgsize;
			snprintf(path, MAXPATHLEN, "%s/%s", cachedir, pkgrepopath);
			if (stat(path, &st) != -1)
				oldsize = st.st_size;
			else
				oldsize = 0;
			dlsize -= oldsize;

			humanize_number(size, sizeof(size), pkgsize, "B", HN_AUTOSCALE, 0);

			printf("\t%s-%s (%" PRId64 "%% of %s)\n", name, newversion, 100 - (100 * oldsize)/pkgsize, size);
			break;
		}
	}

	if (oldsize > newsize) {
		humanize_number(size, sizeof(size), oldsize - newsize, "B", HN_AUTOSCALE, 0);

		switch (type) {
		case PKG_JOBS_INSTALL:
			printf("\nThe installation will free %s\n", size);
			break;
		case PKG_JOBS_UPGRADE:
			printf("\nThe upgrade will free %s\n", size);
			break;
		case PKG_JOBS_DEINSTALL:
		case PKG_JOBS_AUTOREMOVE:
			printf("\nThe deinstallation will free %s\n", size);
			break;
		case PKG_JOBS_FETCH:
			/* nothing to report here */
			break;
		}
	} else if (newsize > oldsize) {
		humanize_number(size, sizeof(size), newsize - oldsize, "B", HN_AUTOSCALE, 0);

		switch (type) {
		case PKG_JOBS_INSTALL:
			printf("\nThe installation will require %s more space\n", size);
			break;
		case PKG_JOBS_UPGRADE:
			printf("\nThe upgrade will require %s more space\n", size);
			break;
		case PKG_JOBS_DEINSTALL:
		case PKG_JOBS_AUTOREMOVE:
			printf("\nThe deinstallation will require %s more space\n", size);
			break;
		case PKG_JOBS_FETCH:
			/* nothing to report here */
			break;
		}
	}

	if ((type == PKG_JOBS_INSTALL) || (type == PKG_JOBS_FETCH) || (type == PKG_JOBS_UPGRADE)) {
		humanize_number(size, sizeof(size), dlsize, "B", HN_AUTOSCALE, 0);
		printf("\n%s to be downloaded\n", size);
	}
}

struct sbuf *
exec_buf(const char *cmd) {
	FILE *fp;
	char buf[BUFSIZ];
	struct sbuf *res;

	if ((fp = popen(cmd, "r")) == NULL)
		return (NULL);

	res = sbuf_new_auto();
	while (fgets(buf, BUFSIZ, fp) != NULL)
		sbuf_cat(res, buf);

	pclose(fp);

	if (sbuf_len(res) == 0) {
		sbuf_delete(res);
		return (NULL);
	}

	sbuf_finish(res);

	return (res);
}

int
hash_file(const char *path, char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	FILE *fp;
	char buffer[BUFSIZ];
	unsigned char hash[SHA256_DIGEST_LENGTH];
	size_t r = 0;
	SHA256_CTX sha256;

	if ((fp = fopen(path, "rb")) == NULL) {
		warn("fopen(%s)", path);
		return (EPKG_FATAL);
	}

	SHA256_Init(&sha256);

	while ((r = fread(buffer, 1, BUFSIZ, fp)) > 0)
		SHA256_Update(&sha256, buffer, r);

	if (ferror(fp) != 0) {
		fclose(fp);
		out[0] = '\0';
		warn("fread(%s)", path);
		return (EPKG_FATAL);
	}

	fclose(fp);

	SHA256_Final(hash, &sha256);

	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
		sprintf(out + (i * 2), "%02x", hash[i]);

	out[SHA256_DIGEST_LENGTH * 2] = '\0';

	return (EPKG_OK);
}
