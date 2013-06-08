/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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
query_tty_yesno(const char *msg, ...)
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
	pkg_vfprintf(tty, msg, ap);
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

bool
query_yesno(const char *msg, ...)
{
	int	 c;
	bool	 r = false;
	va_list	 ap;

	va_start(ap, msg);
	pkg_vprintf(msg, ap);
	va_end(ap);

	c = getchar();
	if (c == 'y' || c == 'Y')
		r = true;
	else if (c == '\n' || c == EOF)
		return false;

	while ((c = getchar()) != '\n' && c != EOF)
		continue;

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
info_flags(unsigned int opt, bool remote)
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
	if (opt & INFO_ANNOTATIONS)
		flags |= PKG_LOAD_ANNOTATIONS;
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
			 PKG_LOAD_SHLIBS_PROVIDED |
			 PKG_LOAD_ANNOTATIONS     |
			 PKG_LOAD_DEPS;
		if (!remote) {
			flags |= PKG_LOAD_FILES  |
				PKG_LOAD_DIRS    |
				PKG_LOAD_USERS   |
				PKG_LOAD_GROUPS  |
				PKG_LOAD_SCRIPTS;
		}
	}

	return flags;
}

void
print_info(struct pkg * const pkg, unsigned int options)
{
	bool print_tag = false;
	bool show_locks = false;
	char size[7];
	const char *reponame, *repourl;
	unsigned opt;
	int64_t flatsize, oldflatsize, pkgsize;
	int cout = 0;		/* Number of characters output */
	int info_num;		/* Number of different data items to print */

	pkg_get(pkg,
		PKG_REPONAME,      &reponame,
		PKG_REPOURL,       &repourl,
		PKG_FLATSIZE,      &flatsize,
		PKG_OLD_FLATSIZE,  &oldflatsize,
		PKG_PKGSIZE,       &pkgsize);

	if (options & INFO_RAW) {
		if (pkg_type(pkg) != PKG_REMOTE)
			pkg_emit_manifest_file(pkg, stdout, 0, NULL);
		else
			pkg_emit_manifest_file(pkg, stdout, PKG_MANIFEST_EMIT_COMPACT, NULL);
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
			cout = pkg_printf("%n-%v", pkg, pkg);
		else if (options & INFO_TAG_ORIGIN)
			cout = pkg_printf("%o", pkg);
		else if (options & INFO_TAG_NAME)
			cout = pkg_printf("%n", pkg);
	}

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
			pkg_printf("%n\n", pkg);
			break;
		case INFO_VERSION:
			if (print_tag)
				printf("%-15s: ", "Version");
			pkg_printf("%v\n", pkg);
			break;
		case INFO_ORIGIN:
			if (print_tag)
				printf("%-15s: ", "Origin");
			pkg_printf("%o\n", pkg);
			break;
		case INFO_PREFIX:
			if (print_tag)
				printf("%-15s: ", "Prefix");
			pkg_printf("%p\n", pkg);
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
				pkg_printf("%C%{%Cn%| %}\n", pkg);
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_LICENSES:
			if (pkg_list_count(pkg, PKG_LICENSES) > 0) {
				if (print_tag)
					printf("%-15s: ", "Licenses");
				pkg_printf("%L%{%Ln%| %l %}\n", pkg);
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_MAINTAINER:
			if (print_tag)
				printf("%-15s: ", "Maintainer");
			pkg_printf("%m\n", pkg);
			break;
		case INFO_WWW:	
			if (print_tag)
				printf("%-15s: ", "WWW");
			pkg_printf("%w\n", pkg);
			break;
		case INFO_COMMENT:
			if (print_tag)
				printf("%-15s: ", "Comment");
			pkg_printf("%c\n", pkg);
			break;
		case INFO_OPTIONS:
			if (pkg_list_count(pkg, PKG_OPTIONS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Options");
				if (quiet) 
					pkg_printf("%O%{%-15On: %Ov\n%|%}", pkg);
				else
					pkg_printf("%O%{\t%-15On: %Ov\n%|%}", pkg);
			}
			break;
		case INFO_SHLIBS_REQUIRED:
			if (pkg_list_count(pkg, PKG_SHLIBS_REQUIRED) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Shared Libs required");
				if (quiet)
					pkg_printf("%B%{%Bn\n%|%}", pkg);
				else
					pkg_printf("%B%{\t%Bn\n%|%}", pkg);
			}
			break;
		case INFO_SHLIBS_PROVIDED:
			if (pkg_list_count(pkg, PKG_SHLIBS_PROVIDED) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Shared Libs provided");
				if (quiet)
					pkg_printf("%b%{%bn\n%|%}", pkg);
				else
					pkg_printf("%b%{\t%bn\n%|%}", pkg);
			}
			break;
		case INFO_ANNOTATIONS:
			if (pkg_list_count(pkg, PKG_ANNOTATIONS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Annotations");
				if (quiet)
					pkg_printf("%A%{%-15An: %Av\n%|%}", pkg);
				else
					pkg_printf("%A%{\t%-15An: %Av\n%|%}", pkg);					
			}
			break;
		case INFO_FLATSIZE:
			if (print_tag)
				printf("%-15s: ", "Flat size");
			pkg_printf("%#sB\n", pkg);
			break;
		case INFO_PKGSIZE: /* Remote pkgs only */
			if (pkg_type(pkg) == PKG_REMOTE) {
				humanize_number(size, sizeof(size),
						pkgsize,"B",
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
			pkg_printf("%e\n", pkg);
			break;
		case INFO_MESSAGE:
			if (print_tag)
				printf("%-15s:\n", "Message");
			pkg_printf("%M\n", pkg);
			break;
		case INFO_DEPS:
			if (pkg_list_count(pkg, PKG_DEPS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Depends on");
				if (quiet) {
					if (show_locks) 
						pkg_printf("%d%{%dn-%dv%#dk\n%|%}", pkg);
					else
						pkg_printf("%d%{%dn-%dv\n%|%}", pkg);
				} else {
					if (show_locks)
						pkg_printf("%d%{\t%dn-%dv%#dk\n%|%}", pkg);
					else
						pkg_printf("%d%{\t%dn-%dv\n%|%}", pkg);
				}
			}
			break;
		case INFO_RDEPS:
			if (pkg_list_count(pkg, PKG_RDEPS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Required by");
				if (quiet) {
					if (show_locks) 
						pkg_printf("%r%{%rn-%rv%#rk\n%|%}", pkg);
					else
						pkg_printf("%r%{%rn-%rv\n%|%}", pkg);
				} else {
					if (show_locks)
						pkg_printf("%r%{\t%rn-%rv%#rk\n%|%}", pkg);
					else
						pkg_printf("%r%{\t%rn-%rv\n%|%}", pkg);
				}
			}
			break;
		case INFO_FILES: /* Installed pkgs only */
			if (pkg_type(pkg) != PKG_REMOTE &&
			    pkg_list_count(pkg, PKG_FILES) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Files");
				if (quiet)
					pkg_printf("%F%{%Fn\n%|%}", pkg);
				else
					pkg_printf("%F%{\t%Fn\n%|%}", pkg);
			}
			break;
		case INFO_DIRS:	/* Installed pkgs only */
			if (pkg_type(pkg) != PKG_REMOTE &&
			    pkg_list_count(pkg, PKG_DIRS) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Directories");
				if (quiet)
					pkg_printf("%D%{%Dn\n%|%}", pkg);
				else
					pkg_printf("%D%{\t%Dn\n%|%}", pkg);
			}
			break;
		case INFO_USERS: /* Installed pkgs only */
			if (pkg_type(pkg) != PKG_REMOTE &&
			    pkg_list_count(pkg, PKG_USERS) > 0) {
				if (print_tag)
					printf("%-15s: ", "Users");
				pkg_printf("%U%{%Un%| %}\n", pkg);
			}
			break;
		case INFO_GROUPS: /* Installed pkgs only */
			if (pkg_type(pkg) != PKG_REMOTE &&
			    pkg_list_count(pkg, PKG_GROUPS) > 0) {
				if (print_tag)
					printf("%-15s: ", "Groups");
				pkg_printf("%G%{%Gn%| %}\n", pkg);
			}
			break;
		case INFO_ARCH:
			if (print_tag)
				printf("%-15s: ", "Architecture");
			pkg_printf("%q\n", pkg);
			break;
		case INFO_REPOURL:
			if (pkg_type(pkg) == PKG_REMOTE &&
			    repourl != NULL && repourl[0] != '\0') {
				if (print_tag)
					printf("%-15s: ", "Pkg URL");
				if (repourl[strlen(repourl) -1] == '/')
					pkg_printf("%S%R\n", repourl, pkg);
				else
					pkg_printf("%S/%R\n", repourl, pkg);
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_LOCKED:
			if (print_tag)
				printf("%-15s: ", "Locked");
			pkg_printf("%?k\n", pkg);
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
	const char *oldversion, *cachedir, *why, *reponame;
	int64_t dlsize, oldsize, newsize;
	int64_t flatsize, oldflatsize, pkgsize;
	char size[7];
	va_list ap;
	pkg_jobs_t type;

	type = pkg_jobs_type(jobs);

	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);

	dlsize = oldsize = newsize = 0;
	flatsize = oldflatsize = pkgsize = 0;
	oldversion = NULL;
	
	pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir);

	while (pkg_jobs(jobs, &pkg) == EPKG_OK) {
		pkg_get(pkg, PKG_OLD_VERSION, &oldversion,
		    PKG_FLATSIZE, &flatsize, PKG_OLD_FLATSIZE, &oldflatsize,
		    PKG_PKGSIZE, &pkgsize, PKG_REASON, &why,
		    PKG_REPONAME, &reponame);

		if (pkg_is_locked(pkg)) {
			pkg_printf("\tPackage %n-%v is locked ", pkg, pkg);
			switch (type) {
			case PKG_JOBS_INSTALL:
			case PKG_JOBS_UPGRADE:
				/* If it's a new install, then it
				 * cannot have been locked yet. */
				if (oldversion != NULL) {
					switch(pkg_version_change(pkg)) {
					case PKG_UPGRADE:
						pkg_printf("and may not be upgraded to version %v\n", pkg);
						break;
					case PKG_REINSTALL:
						printf("and may not be reinstalled\n");
						break;
					case PKG_DOWNGRADE:
						pkg_printf("and may not be downgraded to version %v\n", pkg);
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
			pkg_snprintf(path, MAXPATHLEN, "%S/%R", cachedir, pkg);
			reponame = pkg_repo_ident(pkg_repo_find_name(reponame));

			if (stat(path, &st) == -1 || pkgsize != st.st_size)
				/* file looks corrupted (wrong size),
				   assume a checksum mismatch will
				   occur later and the file will be
				   fetched from remote again */

				dlsize += pkgsize;

			if (oldversion != NULL) {
				switch (pkg_version_change(pkg)) {
				case PKG_DOWNGRADE:
					pkg_printf("\tDowngrading %n: %V -> %v", pkg, pkg, pkg);
					if (pkg_repos_count() > 1)
						printf(" [%s]", reponame);
					printf("\n");
					break;
				case PKG_REINSTALL:
					pkg_printf("\tReinstalling %n-%v", pkg, pkg);
					if (pkg_repos_count() > 1)
						printf(" [%s]", reponame);
					if (why != NULL)
						printf(" (%s)", why);
					printf("\n");
					break;
				case PKG_UPGRADE:
					pkg_printf("\tUpgrading %n: %V -> %v", pkg, pkg, pkg);
					if (pkg_repos_count() > 1)
						printf(" [%s]", reponame);
					printf("\n");
					break;
				}
				oldsize += oldflatsize;
				newsize += flatsize;
			} else {
				newsize += flatsize;

				pkg_printf("\tInstalling %n: %v", pkg, pkg);
				if (pkg_repos_count() > 1)
					printf(" [%s]", reponame);
				printf("\n");
			}
			break;
		case PKG_JOBS_DEINSTALL:
		case PKG_JOBS_AUTOREMOVE:
			oldsize += oldflatsize;
			newsize += flatsize;
			
			pkg_printf("\t%n-%v\n", pkg, pkg);
			break;
		case PKG_JOBS_FETCH:
			dlsize += pkgsize;
			pkg_snprintf(path, MAXPATHLEN, "%S/%R", cachedir, pkg);
			if (stat(path, &st) != -1)
				oldsize = st.st_size;
			else
				oldsize = 0;
			dlsize -= oldsize;

			humanize_number(size, sizeof(size), pkgsize, "B", HN_AUTOSCALE, 0);

			pkg_printf("\t%n-%v ", pkg, pkg);
			printf("(%" PRId64 "%% of %s)\n", 100 - (100 * oldsize)/pkgsize, size);
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
