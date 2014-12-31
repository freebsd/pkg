/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2013-2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <paths.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <errno.h>
#include <pkg.h>

#include <bsd_compat.h>

#include "utlist.h"
#include "pkgcli.h"

bool
query_tty_yesno(bool r, const char *msg, ...)
{
	int	 c;
	va_list	 ap;
	int	 tty_fd;
	FILE	*tty;
	int	 tty_flags = O_RDWR;

#ifdef O_TTY_INIT
	tty_flags |= O_TTY_INIT;
#endif
	tty_fd = open(_PATH_TTY, tty_flags);
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
	else if (c == '\n' || c == EOF) {
		r = false;
		goto cleanup;
	}

	while ((c = getc(tty)) != '\n' && c != EOF)
		continue;

cleanup:
	fclose(tty);

	return (r);
}

static bool
vquery_yesno(bool deft, const char *msg, va_list ap)
{
	char *line = NULL;
	char *out;
	size_t linecap = 0;
	int linelen;
	bool	 r = deft;

	/* We use default value of yes or default in case of quiet mode */
	if (quiet)
		return (yes || r);

	/* Do not query user if we have specified yes flag */
	if (yes)
		return (true);

	pkg_vasprintf(&out, msg, ap);
	printf("%s", out);

	for (;;) {
		if ((linelen = getline(&line, &linecap, stdin)) != -1) {

			if (linelen == 1 && line[0] == '\n') {
				break;
			}
			else if (linelen == 2) {
				if (line[0] == 'y' || line[0] == 'Y') {
					r = true;
					break;
				}
				else if (line[0] == 'n' || line[0] == 'N') {
					r = false;
					break;
				}
			}
			else {
				if (strcasecmp(line, "yes\n") == 0) {
					r = true;
					break;
				}
				else if (strcasecmp(line, "no\n") == 0) {
					r = false;
					break;
				}
			}
			printf("Please type 'Y[es]' or 'N[o]' to make selection\n");
			printf("%s", out);
		}
		else {
			if (errno == EINTR) {
				continue;
			}
			else {
				/* Assume EOF as false */
				r = false;
				break;
			}
		}
	}

	free(out);

	return (r);
}

bool
query_yesno(bool deft, const char *msg, ...)
{
	va_list	 ap;
	bool r;

	va_start(ap, msg);
	r = vquery_yesno(deft, msg, ap);
	va_end(ap);

	return (r);
}

int
query_select(const char *msg, const char **opts, int ncnt, int deft)
{
	int i;
	char *str = NULL;
	char *endpntr = NULL;
	size_t n = 0;

	printf("%s\n", msg);
	for (i = 0; i < ncnt; i++) {
		if (i + 1 == deft)
		{
			printf("*[%d] %s\n",
				i + 1, opts[i]);
		} else {
			printf(" [%d] %s\n",
				i + 1, opts[i]);
		}
	}

	i = deft;
	while (getline(&str, &n, stdin) == -1) {
		if (errno == EINTR)
			continue;
		else
			goto cleanup;
	}
	i = (int) strtoul(str, &endpntr, 10);

	if (endpntr == NULL || *endpntr == '\0') {
		i = deft;
	} else if (*endpntr == '\n' || *endpntr == '\r') {
		if (i > ncnt || i < 1)
			i = deft;
	} else
		i = -1;

cleanup:
	free(str);
	return (i);
}

/* what the pkg needs to load in order to display the requested info */
int
info_flags(uint64_t opt, bool remote)
{
	int flags = PKG_LOAD_BASIC;

	if (opt & INFO_CATEGORIES)
		flags |= PKG_LOAD_CATEGORIES;
	if (opt & INFO_LICENSES)
		flags |= PKG_LOAD_LICENSES;
	if (opt & (INFO_OPTIONS|INFO_OPTION_DEFAULTS|INFO_OPTION_DESCRIPTIONS))
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
print_info(struct pkg * const pkg, uint64_t options)
{
	bool print_tag = false;
	bool show_locks = false;
	char size[7];
	const char *repourl;
	unsigned opt;
	int64_t flatsize, oldflatsize, pkgsize;
	int cout = 0;		/* Number of characters output */
	int info_num;		/* Number of different data items to print */
	int outflags = 0;

	pkg_get(pkg,
		PKG_REPOURL,       &repourl,
		PKG_FLATSIZE,      &flatsize,
		PKG_OLD_FLATSIZE,  &oldflatsize,
		PKG_PKGSIZE,       &pkgsize);

	if (options & INFO_RAW) {
		switch (options & (INFO_RAW_YAML|INFO_RAW_JSON|INFO_RAW_JSON_COMPACT|INFO_RAW_UCL)) {
		case INFO_RAW_YAML:
			outflags |= PKG_MANIFEST_EMIT_PRETTY;
			break;
		case INFO_RAW_UCL:
			outflags |= PKG_MANIFEST_EMIT_UCL;
			break;
		case INFO_RAW_JSON:
			outflags |= PKG_MANIFEST_EMIT_JSON;
		case INFO_RAW_JSON_COMPACT:
			break;
		}
		if (pkg_type(pkg) == PKG_REMOTE)
			outflags |= PKG_MANIFEST_EMIT_COMPACT;

		pkg_emit_manifest_file(pkg, stdout, outflags, NULL);

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
		case INFO_INSTALLED:
			if (print_tag)
				printf("%-15s: ", "Installed on");
			pkg_printf("%t%{%+%}\n", pkg);
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
				pkg_printf("%N [%S]\n", pkg, repourl);
			} else if (!print_tag)
				printf("\n");
			break;
		case INFO_CATEGORIES:
			if (print_tag)
				printf("%-15s: ", "Categories");
			pkg_printf("%C%{%Cn%| %}\n", pkg);
			break;
		case INFO_LICENSES:
			if (print_tag)
				printf("%-15s: ", "Licenses");
			pkg_printf("%L%{%Ln%| %l %}\n", pkg);
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
			if (print_tag)
				printf("%-15s:\n", "Annotations");
			if (quiet)
				pkg_printf("%A%{%-15An: %Av\n%|%}", pkg);
			else
				pkg_printf("%A%{\t%-15An: %Av\n%|%}", pkg);
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
			if (pkg_has_message(pkg))
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

enum pkg_display_type {
	PKG_DISPLAY_LOCKED = 0,
	PKG_DISPLAY_DELETE,
	PKG_DISPLAY_INSTALL,
	PKG_DISPLAY_UPGRADE,
	PKG_DISPLAY_DOWNGRADE,
	PKG_DISPLAY_REINSTALL,
	PKG_DISPLAY_FETCH,
	PKG_DISPLAY_MAX
};
struct pkg_solved_display_item {
	struct pkg *new, *old;
	enum pkg_display_type display_type;
	pkg_solved_t solved_type;
	struct pkg_solved_display_item *prev, *next;
};

static void
set_jobs_summary_pkg(struct pkg_jobs *jobs,
		struct pkg *new_pkg, struct pkg *old_pkg,
		pkg_solved_t type, int64_t *oldsize,
		int64_t *newsize, int64_t *dlsize,
		struct pkg_solved_display_item **disp)
{
	const char *oldversion, *repopath, *destdir;
	char path[MAXPATHLEN];
	int ret;
	struct stat st;
	int64_t flatsize, oldflatsize, pkgsize;
	struct pkg_solved_display_item *it;

	flatsize = oldflatsize = pkgsize = 0;
	oldversion = NULL;

	pkg_get(new_pkg, PKG_FLATSIZE, &flatsize, PKG_PKGSIZE, &pkgsize,
		PKG_REPOPATH, &repopath);
	if (old_pkg != NULL)
		pkg_get(old_pkg, PKG_VERSION, &oldversion, PKG_FLATSIZE, &oldflatsize);

	it = malloc(sizeof (*it));
	if (it == NULL) {
		fprintf(stderr, "malloc failed for "
				"pkg_solved_display_item: %s", strerror (errno));
		return;
	}
	it->new = new_pkg;
	it->old = old_pkg;
	it->solved_type = type;

	if (old_pkg != NULL && pkg_is_locked(old_pkg)) {
		it->display_type = PKG_DISPLAY_LOCKED;
		DL_APPEND(disp[it->display_type], it);
		return;
	}

	destdir = pkg_jobs_destdir(jobs);

	switch (type) {
	case PKG_SOLVED_INSTALL:
	case PKG_SOLVED_UPGRADE:
		ret = EPKG_FATAL;

		if (destdir == NULL)
			ret = pkg_repo_cached_name(new_pkg, path, sizeof(path));
		else if (repopath != NULL) {
			snprintf(path, sizeof(path), "%s/%s", destdir, repopath);
			ret = EPKG_OK;
		}

		if (ret == EPKG_OK && (stat(path, &st) == -1 || pkgsize != st.st_size))
			/* file looks corrupted (wrong size),
					   assume a checksum mismatch will
					   occur later and the file will be
					   fetched from remote again */
			*dlsize += pkgsize;

		if (old_pkg != NULL) {
			switch (pkg_version_change_between(new_pkg, old_pkg)) {
			case PKG_DOWNGRADE:
				it->display_type = PKG_DISPLAY_DOWNGRADE;
				break;
			case PKG_REINSTALL:
				it->display_type = PKG_DISPLAY_REINSTALL;

				break;
			case PKG_UPGRADE:
				it->display_type = PKG_DISPLAY_UPGRADE;

				break;
			}
			*oldsize += oldflatsize;
			*newsize += flatsize;
		} else {
			it->display_type = PKG_DISPLAY_INSTALL;
			*newsize += flatsize;
		}
		break;
	case PKG_SOLVED_DELETE:
		*oldsize += flatsize;
		it->display_type = PKG_DISPLAY_DELETE;

		break;
	case PKG_SOLVED_UPGRADE_REMOVE:
		/* Ignore split-upgrade packages for display */
		free(it);
		return;
		break;

	case PKG_SOLVED_FETCH:
		*newsize += pkgsize;
		it->display_type = PKG_DISPLAY_FETCH;

		if (destdir == NULL)
			pkg_repo_cached_name(new_pkg, path, sizeof(path));
		else
			snprintf(path, sizeof(path), "%s/%s", destdir, repopath);

		if (stat(path, &st) != -1) {
			*oldsize += st.st_size;

			if (pkgsize != st.st_size)
				*dlsize += pkgsize;
			else {
				free(it);
				return;
			}
		}
		else
			*dlsize += pkgsize;

		break;
	}
	DL_APPEND(disp[it->display_type], it);
}

static void
display_summary_item (struct pkg_solved_display_item *it, int64_t total_size,
		int64_t dlsize)
{
	const char *why;
	int64_t pkgsize;
	char size[7], tlsize[7];

	pkg_get(it->new, PKG_PKGSIZE, &pkgsize);

	switch (it->display_type) {
	case PKG_DISPLAY_LOCKED:
		pkg_printf("\tPackage %n-%v is locked ", it->old, it->old);
		switch (it->solved_type) {
		case PKG_SOLVED_INSTALL:
		case PKG_SOLVED_UPGRADE:
			/* If it's a new install, then it
			 * cannot have been locked yet. */
			pkg_printf("and may not be upgraded to version %v\n", it->new);
			break;
		case PKG_SOLVED_DELETE:
		case PKG_SOLVED_UPGRADE_REMOVE:
			printf("and may not be deinstalled\n");
			return;
			break;
		case PKG_SOLVED_FETCH:
			printf("but a new package can still be fetched\n");
			break;
		}
		break;
	case PKG_DISPLAY_DELETE:
		pkg_get(it->new, PKG_REASON, &why);
		pkg_printf("\t%n-%v", it->new, it->new);
		if (why != NULL)
			printf(" (%s)", why);
		printf("\n");
		break;
	case PKG_DISPLAY_INSTALL:
		pkg_printf("\t%n: %v", it->new, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		printf("\n");
		break;
	case PKG_DISPLAY_UPGRADE:
		pkg_printf("\t%n: %v -> %v", it->new, it->old, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		printf("\n");
		break;
	case PKG_DISPLAY_DOWNGRADE:
		pkg_printf("\t%n: %v -> %v", it->new, it->old, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		printf("\n");
		break;
	case PKG_DISPLAY_REINSTALL:
		pkg_get(it->new, PKG_REASON, &why);
		pkg_printf("\t%n-%v", it->new, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		if (why != NULL)
			printf(" (%s)", why);
		printf("\n");
		break;
	case PKG_DISPLAY_FETCH:
		humanize_number(size, sizeof(size), pkgsize, "B", HN_AUTOSCALE, 0);
		humanize_number(tlsize, sizeof(size), dlsize, "B", HN_AUTOSCALE, 0);

		pkg_printf("\t%n-%v ", it->new, it->new);
		printf("(%.2f%% of %s: %s)\n", ((double)100 * pkgsize) / (double)dlsize,
				tlsize, size);
		break;
	default:
		break;
	}
}


static const char* pkg_display_messages[PKG_DISPLAY_MAX + 1] = {
	[PKG_DISPLAY_LOCKED] = "Installed packages LOCKED",
	[PKG_DISPLAY_DELETE] = "Installed packages to be REMOVED",
	[PKG_DISPLAY_INSTALL] = "New packages to be INSTALLED",
	[PKG_DISPLAY_UPGRADE] = "Installed packages to be UPGRADED",
	[PKG_DISPLAY_DOWNGRADE] = "Installed packages to be DOWNGRADED",
	[PKG_DISPLAY_REINSTALL] = "Installed packages to be REINSTALLED",
	[PKG_DISPLAY_FETCH] = "New packages to be FETCHED",
	[PKG_DISPLAY_MAX] = NULL
};

int
print_jobs_summary(struct pkg_jobs *jobs, const char *msg, ...)
{
	struct pkg *new_pkg, *old_pkg;
	void *iter = NULL;
	char size[7];
	va_list ap;
	int type, displayed = 0;
	int64_t dlsize, oldsize, newsize;
	struct pkg_solved_display_item *disp[PKG_DISPLAY_MAX], *cur, *tmp;
	bool first = true;

	dlsize = oldsize = newsize = 0;
	type = pkg_jobs_type(jobs);
	memset(disp, 0, sizeof (disp));

	while (pkg_jobs_iter(jobs, &iter, &new_pkg, &old_pkg, &type))
		set_jobs_summary_pkg(jobs, new_pkg, old_pkg, type, &oldsize,
			&newsize, &dlsize, disp);

	for (type = 0; type < PKG_DISPLAY_MAX; type ++) {
		if (disp[type] != NULL) {
			/* Space between each section. */
			if (!first)
				puts("");
			else
				first = false;
			if (msg != NULL) {
				va_start(ap, msg);
				vprintf(msg, ap);
				va_end(ap);
				fflush(stdout);
				msg = NULL;
			}
			printf("%s:\n", pkg_display_messages[type]);
			DL_FOREACH_SAFE(disp[type], cur, tmp) {
				display_summary_item(cur, newsize, dlsize);
				displayed ++;
				free(cur);
			}
		}
	}

	/* Add an extra line before the size output. */
	if (oldsize != newsize || dlsize)
		puts("");

	if (oldsize > newsize) {
		humanize_number(size, sizeof(size), oldsize - newsize, "B", HN_AUTOSCALE, 0);
		printf("The operation will free %s.\n", size);
	} else if (newsize > oldsize) {
		humanize_number(size, sizeof(size), newsize - oldsize, "B", HN_AUTOSCALE, 0);
		printf("The process will require %s more space.\n", size);
	}

	if (dlsize > 0) {
		humanize_number(size, sizeof(size), dlsize, "B", HN_AUTOSCALE, 0);
		printf("%s to be downloaded.\n", size);
	}

	return (displayed);
}

void
sbuf_flush(struct sbuf *buf)
{
	sbuf_finish(buf);
	printf("%s", sbuf_data(buf));
	sbuf_clear(buf);
}
