/*-
 * Copyright (c) 2011-2024 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2015 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2013-2016 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pwd.h>
#include <pkg.h>
#include <xmalloc.h>

#include <bsd_compat.h>

#include "pkgcli.h"

struct jobs_sum_number {
	int install;
	int reinstall;
	int downgrade;
	int upgrade;
	int delete;
	int fetch;
	int group_install;
	int group_upgrade;
};

void
append_yesno(bool r, char *yesnomsg, size_t len)
{
	static const char	trunc[] = "\n[truncated] ";
	/* These two strings must be the same length. */
	static const char	yes[] = "[Y/n]: ";
	static const char	no[] = "[y/N]: ";

	size_t	msglen = strlen(yesnomsg);

	if (msglen > len - sizeof yes) {
		yesnomsg[len - sizeof trunc - sizeof yes] = '\0';
		strlcat(yesnomsg, trunc, len);
	}
	strlcat(yesnomsg, r ? yes : no, len);
}

bool
query_tty_yesno(bool r, const char *msg, ...)
{
	int	 c;
	va_list	 ap;
	int	 tty_fd;
	FILE	*tty;
	int	 tty_flags = O_RDWR;
	char	yesnomsg[65536];

#ifdef O_TTY_INIT
	tty_flags |= O_TTY_INIT;
#endif
	tty_fd = open(_PATH_TTY, tty_flags);
	if (tty_fd == -1) {
		/* No ctty -- return the default answer */
		if (default_yes)
			return (true);
		return (r);
	}

	tty = fdopen(tty_fd, "r+");

	strlcpy(yesnomsg, msg, sizeof(yesnomsg));
	append_yesno(default_yes || r, yesnomsg, sizeof yesnomsg);

	va_start(ap, msg);
	pkg_vfprintf(tty, yesnomsg, ap);
	va_end(ap);

	fflush(tty);
	c = getc(tty);
	if (c == 'y' || c == 'Y')
		r = true;
	else if (c == 'n' || c == 'N')
		r = false;
	else if (c == '\n' || c == EOF) {
                if (default_yes)
			r = true;
		/* Else, r is not modified. It's default value is kept. */
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
	char	yesnomsg[65536];

	/* We use default value of yes or default in case of quiet mode */
	if (quiet)
		return (yes || default_yes || r);

	if (dry_run)
		return (yes || default_yes || r );

	/* Do not query user if we have specified yes flag */
	if (yes)
		return (true);

	strlcpy(yesnomsg, msg, sizeof(yesnomsg));
	append_yesno(default_yes || r, yesnomsg, sizeof yesnomsg);

	pkg_vasprintf(&out, yesnomsg, ap);
	printf("%s", out);

	for (;;) {
		if ((linelen = getline(&line, &linecap, stdin)) != -1) {

			if (linelen == 1 && line[0] == '\n') {
				if (default_yes)
					r = true;
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
				if (STRIEQ(line, "yes\n")) {
					r = true;
					break;
				}
				else if (STRIEQ(line, "no\n")) {
					r = false;
					break;
				}
			}
			printf("Please type 'Y[es]' or 'N[o]' to make a selection\n");
			printf("%s", out);
		}
		else {
			if (errno == EINTR) {
				continue;
			} else {
				if (default_yes) {
					r = true;
				/* Else, assume EOF as false */
				} else {
					r = false;
				}
				break;
			}
		}
	}

	free(line);
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
	if (opt & INFO_PROVIDED)
		flags |= PKG_LOAD_PROVIDES;
	if (opt & INFO_REQUIRED)
		flags |= PKG_LOAD_REQUIRES;
	if (opt & INFO_ANNOTATIONS)
		flags |= PKG_LOAD_ANNOTATIONS;
	if (opt & INFO_DEPS)
		flags |= PKG_LOAD_DEPS|PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_REQUIRES;
	if (opt & INFO_RDEPS)
		flags |= PKG_LOAD_RDEPS|PKG_LOAD_SHLIBS_PROVIDED|PKG_LOAD_PROVIDES;
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
			 PKG_LOAD_PROVIDES        |
			 PKG_LOAD_REQUIRES        |
			 PKG_LOAD_ANNOTATIONS     |
			 PKG_LOAD_DEPS;
		if (!remote) {
			flags |= PKG_LOAD_FILES  |
				PKG_LOAD_DIRS    |
				PKG_LOAD_USERS   |
				PKG_LOAD_GROUPS  |
				PKG_LOAD_SCRIPTS |
				PKG_LOAD_LUA_SCRIPTS;
		}
	}

	return flags;
}

void
print_info(struct pkgdb *db, struct pkg * const pkg, uint64_t options)
{
	bool print_tag = false;
	bool show_locks = false;
	bool is_group = false;
	const char *repourl = NULL;
	unsigned opt;
	int cout = 0;		/* Number of characters output */
	int info_num;		/* Number of different data items to print */
	int outflags = PKG_MANIFEST_EMIT_LOCAL_METADATA;

	pkg_get(pkg, PKG_ATTR_REPOURL, &repourl);

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
			break;
		case INFO_RAW_JSON_COMPACT:
			break;
		default:
			outflags |= PKG_MANIFEST_EMIT_UCL;
		}
		if (pkg_type(pkg) == PKG_REMOTE)
			outflags |= PKG_MANIFEST_EMIT_COMPACT;

		pkg_emit_manifest_file(pkg, stdout, outflags);
		if (outflags & PKG_MANIFEST_EMIT_COMPACT)
			printf("\n");
		return;
	}

	/* Show locking status when requested to display it and the
	   package is locally installed */
	if (pkg_type(pkg) == PKG_INSTALLED && (options & INFO_LOCKED) != 0)
		show_locks = true;
	if (pkg_type(pkg) == PKG_GROUP_REMOTE || pkg_type(pkg) == PKG_GROUP_INSTALLED)
		is_group = true;

	if (!quiet) {
		/* Print a tag-line identifying the package -- either
		   NAMEVER, ORIGIN or NAME (in that order of
		   preference).  This may be the only output from this
		   function */

		if (options & INFO_TAG_NAMEVER) {
			if (is_group)
				cout = pkg_printf("@%n", pkg);
			else
				cout = pkg_printf("%n-%v", pkg, pkg);
		}
		else if (options & INFO_TAG_ORIGIN) {
			if (is_group)
				return;
			cout = pkg_printf("%o", pkg);
		}
		else if (options & INFO_TAG_NAME) {
			if (is_group)
				cout = pkg_printf("@%n", pkg);
			else
				cout = pkg_printf("%n", pkg);
		}
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
	        putchar('\n');
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
		        putchar('\n');
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
			if (pkg_type(pkg) == PKG_INSTALLED) {
				if (print_tag) {
					printf("%-15s: ", "Installed on");
					pkg_printf("%t%{%c %Z%}\n", pkg);
				}
			} else if (!print_tag)
				putchar('\n');
			break;
		case INFO_VERSION:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s: ", "Version");
			pkg_printf("%v\n", pkg);
			break;
		case INFO_ORIGIN:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s: ", "Origin");
			pkg_printf("%o\n", pkg);
			break;
		case INFO_PREFIX:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s: ", "Prefix");
			pkg_printf("%p\n", pkg);
			break;
		case INFO_REPOSITORY:
			if (is_group)
				break;
			if (pkg_type(pkg) == PKG_REMOTE &&
			    repourl != NULL && repourl[0] != '\0') {
				if (print_tag)
					printf("%-15s: ", "Repository");
				pkg_printf("%N [%S]\n", pkg, repourl);
			} else if (!print_tag)
				putchar('\n');
			break;
		case INFO_CATEGORIES:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s: ", "Categories");
			pkg_printf("%C%{%Cn%| %}\n", pkg);
			break;
		case INFO_LICENSES:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s: ", "Licenses");
			pkg_printf("%L%{%Ln%| %l %}\n", pkg);
			break;
		case INFO_MAINTAINER:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s: ", "Maintainer");
			pkg_printf("%m\n", pkg);
			break;
		case INFO_WWW:
			if (is_group)
				break;
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
		case INFO_REQUIRED:
			if (pkg_list_count(pkg, PKG_REQUIRES) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Requires");
				if (quiet)
					pkg_printf("%Y%{%Yn\n%|%}", pkg);
				else
					pkg_printf("%Y%{\t%Yn\n%|%}", pkg);
			}
			break;
		case INFO_PROVIDED:
			if (pkg_list_count(pkg, PKG_PROVIDES) > 0) {
				if (print_tag)
					printf("%-15s:\n", "Provides");
				if (quiet)
					pkg_printf("%y%{%yn\n%|%}", pkg);
				else
					pkg_printf("%y%{\t%yn\n%|%}", pkg);
			}
			break;
		case INFO_ANNOTATIONS:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s:\n", "Annotations");
			if (quiet)
				pkg_printf("%A%{%-15An: %Av\n%|%}", pkg);
			else
				pkg_printf("%A%{\t%-15An: %Av\n%|%}", pkg);
			break;
		case INFO_FLATSIZE:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s: ", "Flat size");
			pkg_printf("%#sB\n", pkg);
			break;
		case INFO_PKGSIZE: /* Remote pkgs only */
			if (pkg_type(pkg) == PKG_REMOTE) {
				if (print_tag)
					printf("%-15s: ", "Pkg size");
				pkg_printf("%#xB\n", pkg);
			} else if (!print_tag)
			        putchar('\n');
			break;
		case INFO_DESCR:
			if (is_group)
				break;
			if (print_tag)
				printf("%-15s:\n", "Description");
			pkg_printf("%e\n", pkg);
			break;
		case INFO_MESSAGE:
			if (is_group)
				break;
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
			if (db != NULL) {
				struct pkg_stringlist *sl = NULL;
				struct pkg_stringlist_iterator	*slit;
				struct pkgbase *pb;
				bool shlibstag = false;
				bool reqtag = false;
				const char *buf;
				struct pkgdb_it *it;
				struct pkg *p = NULL;

				pkg_get(pkg, PKG_ATTR_SHLIBS_REQUIRED, &sl);
				pb = pkgbase_new(db);
				slit = pkg_stringlist_iterator(sl);
				while ((buf = pkg_stringlist_next(slit))) {
					if (pkgbase_provide_shlib(pb, buf))
						continue;
					if (!shlibstag) {
						if (print_tag)
							printf("%-15s:\n", "Shared library requirements");
						shlibstag = true;
					}
					it = pkgdb_query_shlib_provide(db, buf);
					while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
						pkg_printf("\t%n-%v (%S)\n", p, p, buf);
					}
					pkgdb_it_free(it);
				}
				free(slit);
				free(sl);

				pkg_get(pkg, PKG_ATTR_REQUIRES, &sl);
				slit = pkg_stringlist_iterator(sl);
				while ((buf = pkg_stringlist_next(slit))) {
					if (!reqtag) {
						if (print_tag)
							printf("%-15s:\n", "Requirements");
						reqtag = true;
					}
					it = pkgdb_query_provide(db, buf);
					while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
						pkg_printf("\t%n-%v (%S)\n", p, p, buf);
					}
					pkgdb_it_free(it);
				}
				free(slit);
				free(sl);
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
			if (db != NULL) {
				struct pkg_stringlist *sl = NULL;
				struct pkg_stringlist_iterator	*slit;
				struct pkgbase *pb;
				bool shlibstag = false;
				bool reqtag = false;
				const char *buf;
				struct pkgdb_it *it;
				struct pkg *p = NULL;

				pkg_get(pkg, PKG_ATTR_SHLIBS_PROVIDED, &sl);
				pb = pkgbase_new(db);
				slit = pkg_stringlist_iterator(sl);
				while ((buf = pkg_stringlist_next(slit))) {
					if (pkgbase_provide_shlib(pb, buf))
						continue;
					if (!shlibstag) {
						if (print_tag)
							printf("%-15s:\n", "Shared library provided");
						shlibstag = true;
					}
					it = pkgdb_query_shlib_require(db, buf);
					while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
						pkg_printf("\t%n-%v (%S)\n", p, p, buf);
					}
					pkgdb_it_free(it);
				}
				free(slit);
				free(sl);

				pkg_get(pkg, PKG_ATTR_PROVIDES, &sl);
				slit = pkg_stringlist_iterator(sl);
				while ((buf = pkg_stringlist_next(slit))) {
					if (!reqtag) {
						if (print_tag)
							printf("%-15s:\n", "Provides");
						reqtag = true;
					}
					it = pkgdb_query_require(db, buf);
					while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
						pkg_printf("\t%n-%v (%S)\n", p, p, buf);
					}
					pkgdb_it_free(it);
				}
				free(slit);
				free(sl);
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
			if (is_group)
				break;
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
				putchar('\n');
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
	PKG_DISPLAY_INSTALL,
	PKG_DISPLAY_UPGRADE,
	PKG_DISPLAY_DOWNGRADE,
	PKG_DISPLAY_REINSTALL,
	PKG_DISPLAY_DELETE,
	PKG_DISPLAY_FETCH,
	PKG_DISPLAY_GROUP_INSTALL,
	PKG_DISPLAY_GROUP_UPGRADE,
	PKG_DISPLAY_MAX
};
struct pkg_solved_display {
	struct pkg *new, *old;
	enum pkg_display_type display_type;
	pkg_solved_t solved_type;
};

typedef vec_t(struct pkg_solved_display *) pkg_solved_display_t;

static void
set_jobs_summary_pkg(struct pkg_jobs *jobs, struct pkg *new_pkg,
    struct pkg *old_pkg, pkg_solved_t type, int64_t *oldsize,
    int64_t *newsize, int64_t *dlsize, pkg_solved_display_t *disp,
    struct jobs_sum_number *sum)
{
	const char *repopath = NULL, *destdir;
	char path[MAXPATHLEN];
	int ret;
	struct stat st;
	int64_t flatsize, oldflatsize, pkgsize;
	struct pkg_solved_display *it;

	flatsize = oldflatsize = pkgsize = 0;

	pkg_get(new_pkg, PKG_ATTR_FLATSIZE, &flatsize);
	pkg_get(new_pkg, PKG_ATTR_PKGSIZE, &pkgsize);
	pkg_get(new_pkg, PKG_ATTR_REPOPATH, &repopath);
	if (old_pkg != NULL)
		pkg_get(old_pkg, PKG_ATTR_FLATSIZE, &oldflatsize);

	it = malloc(sizeof (*it));
	if (it == NULL) {
		fprintf(stderr, "malloc failed for "
				"pkg_solved_display: %s", strerror (errno));
		return;
	}
	it->new = new_pkg;
	it->old = old_pkg;
	it->solved_type = type;
	it->display_type = PKG_DISPLAY_MAX;

	if (old_pkg != NULL && pkg_is_locked(old_pkg)) {
		it->display_type = PKG_DISPLAY_LOCKED;
		vec_push(&disp[it->display_type], it);
		return;
	}

	destdir = pkg_jobs_destdir(jobs);

	switch (type) {
	case PKG_SOLVED_INSTALL:
	case PKG_SOLVED_UPGRADE:
		if (destdir == NULL)
			ret = pkg_repo_cached_name(new_pkg, path, sizeof(path));
		else if (repopath != NULL) {
			snprintf(path, sizeof(path), "%s/%s", destdir, repopath);
			ret = EPKG_OK;
		} else
			break;

		if ((ret == EPKG_OK || ret == EPKG_FATAL) && (stat(path, &st) == -1 || pkgsize != st.st_size)) {
			/* file looks corrupted (wrong size),
					   assume a checksum mismatch will
					   occur later and the file will be
					   fetched from remote again */
			*dlsize += pkgsize;
			nbtodl += 1;
		}

		if (pkg_type(new_pkg) == PKG_GROUP_REMOTE) {
			if (old_pkg == NULL) {
				it->display_type = PKG_DISPLAY_GROUP_INSTALL;
				sum->group_install++;
			} else {
				it->display_type = PKG_DISPLAY_GROUP_UPGRADE;
				sum->group_upgrade++;
			}
		} else {
			if (old_pkg != NULL) {
				switch (pkg_version_change_between(new_pkg, old_pkg)) {
				case PKG_DOWNGRADE:
					it->display_type = PKG_DISPLAY_DOWNGRADE;
					sum->downgrade++;
					break;
				case PKG_REINSTALL:
					it->display_type = PKG_DISPLAY_REINSTALL;
					sum->reinstall++;
					break;
				case PKG_UPGRADE:
					it->display_type = PKG_DISPLAY_UPGRADE;
					sum->upgrade++;
					break;
				}
				*oldsize += oldflatsize;
				*newsize += flatsize;
			} else {
				it->display_type = PKG_DISPLAY_INSTALL;
				sum->install++;
				*newsize += flatsize;
			}
		}
		break;
	case PKG_SOLVED_DELETE:
		*oldsize += flatsize;
		it->display_type = PKG_DISPLAY_DELETE;
		sum->delete++;
		break;
	case PKG_SOLVED_UPGRADE_INSTALL:
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
		sum->fetch++;

		break;
	}
	vec_push(&disp[it->display_type], it);
}

static void
display_summary_item(struct pkg_solved_display *it, int64_t dlsize)
{
	const char *why = NULL;
	int64_t pkgsize = 0;
	char size[8], tlsize[8];
	const char *type;

	pkg_get(it->new, PKG_ATTR_PKGSIZE, &pkgsize);

	switch (it->display_type) {
	case PKG_DISPLAY_LOCKED:
		pkg_printf("\tPackage %n-%v is locked ", it->old, it->old);
		switch (it->solved_type) {
		case PKG_SOLVED_INSTALL:
		case PKG_SOLVED_UPGRADE:
		case PKG_SOLVED_UPGRADE_INSTALL:
			/* If it's a new install, then it
			 * cannot have been locked yet. */
			switch (pkg_version_change_between(it->old, it->new)) {
			case PKG_DOWNGRADE:
				type = "downgraded";
				break;
			case PKG_REINSTALL:
				type = "reinstalled";
				break;
			case PKG_UPGRADE:
				type = "upgraded";
				break;
			default: /* appease compiler warnings */
				type = "upgraded";
				break;
			}
			pkg_printf("and may not be %S to version %v\n", type,
			    it->new);
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
		pkg_get(it->new, PKG_ATTR_REASON, &why);
		pkg_printf("\t%n: %v", it->new, it->new);
		if (why != NULL)
			printf(" (%s)", why);
		putchar('\n');
		break;
	case PKG_DISPLAY_INSTALL:
		pkg_printf("\t%n: %v", it->new, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		putchar('\n');
		break;
	case PKG_DISPLAY_UPGRADE:
		pkg_printf("\t%n: %v -> %v", it->new, it->old, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		putchar('\n');
		break;
	case PKG_DISPLAY_DOWNGRADE:
		pkg_printf("\t%n: %v -> %v", it->new, it->old, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		putchar('\n');
		break;
	case PKG_DISPLAY_REINSTALL:
		pkg_get(it->new, PKG_ATTR_REASON, &why);
		pkg_printf("\t%n-%v", it->new, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		if (why != NULL)
			printf(" (%s)", why);
		putchar('\n');
		break;
	case PKG_DISPLAY_FETCH:
		humanize_number(size, sizeof(size), pkgsize, "B",
		    HN_AUTOSCALE, HN_IEC_PREFIXES);
		humanize_number(tlsize, sizeof(size), dlsize, "B",
		    HN_AUTOSCALE, HN_IEC_PREFIXES);

		pkg_printf("\t%n: %v ", it->new, it->new);
		printf("(%s: %.2f%% of the %s to download)\n", size,
		    ((double)100 * pkgsize) / (double)dlsize, tlsize);
		break;
	case PKG_DISPLAY_GROUP_UPGRADE:
		pkg_printf("\t%n", it->new, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		putchar('\n');
		break;
	case PKG_DISPLAY_GROUP_INSTALL:
		pkg_printf("\t@%n", it->new, it->new);
		if (pkg_repos_total_count() > 1)
			pkg_printf(" [%N]", it->new);
		putchar('\n');
		break;
	default:
		break;
	}
}


static const char* pkg_display_messages[PKG_DISPLAY_MAX + 1] = {
	[PKG_DISPLAY_LOCKED] = "Installed packages LOCKED",
	[PKG_DISPLAY_DELETE] = "Installed packages to be REMOVED",
	[PKG_DISPLAY_INSTALL] = "New packages to be INSTALLED",
	[PKG_DISPLAY_GROUP_UPGRADE] = "New groups to be UPGRADED",
	[PKG_DISPLAY_DOWNGRADE] = "Installed packages to be DOWNGRADED",
	[PKG_DISPLAY_REINSTALL] = "Installed packages to be REINSTALLED",
	[PKG_DISPLAY_FETCH] = "New packages to be FETCHED",
	[PKG_DISPLAY_GROUP_INSTALL] = "New groups to be INSTALLED",
	[PKG_DISPLAY_UPGRADE] = "Installed packages to be UPGRADED",
	[PKG_DISPLAY_MAX] = NULL
};

static int
namecmp(const void *a, const void *b)
{
	const struct pkg_solved_display *sda = *(const struct pkg_solved_display **) a;
	const struct pkg_solved_display *sdb = *(const struct pkg_solved_display **) b;

	return (pkg_namecmp(sda->new, sdb->new));
}

int
print_jobs_summary(struct pkg_jobs *jobs, const char *msg, ...)
{
	struct pkg *new_pkg, *old_pkg;
	void *iter = NULL;
	char size[8];
	va_list ap;
	int type, displayed = 0;
	int64_t dlsize, oldsize, newsize;
	pkg_solved_display_t disp[PKG_DISPLAY_MAX];
	bool first = true;
	size_t bytes_change, limbytes;
	struct jobs_sum_number sum;

	dlsize = oldsize = newsize = 0;
	type = pkg_jobs_type(jobs);
	memset(disp, 0, sizeof(*disp) * PKG_DISPLAY_MAX);
	memset(&sum, 0, sizeof(sum));

	nbtodl = 0;
	while (pkg_jobs_iter(jobs, &iter, &new_pkg, &old_pkg, &type)) {
		set_jobs_summary_pkg(jobs, new_pkg, old_pkg, type, &oldsize,
		&newsize, &dlsize, disp, &sum);
	}

	for (type = 0; type < PKG_DISPLAY_MAX; type ++) {
		if (disp[type].len != 0) {
			/* Space between each section. */
			if (!first)
				putchar('\n');
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
			qsort(disp[type].d, disp[type].len, sizeof(disp[type].d[0]), namecmp);
			vec_foreach(disp[type], i) {
				display_summary_item(disp[type].d[i], dlsize);
				displayed ++;
			}
			vec_free_and_free(&disp[type], free);
		}
	}

	limbytes = pkg_object_int(pkg_config_get("WARN_SIZE_LIMIT"));
	bytes_change = (size_t)llabs(newsize - oldsize);

	putchar('\n');
	if (sum.delete > 0)
		printf("Number of packages to be removed: %d\n", sum.delete);
	if (sum.install > 0)
		printf("Number of packages to be installed: %d\n", sum.install);
	if (sum.upgrade > 0)
		printf("Number of packages to be upgraded: %d\n", sum.upgrade);
	if (sum.reinstall > 0)
		printf("Number of packages to be reinstalled: %d\n",
		    sum.reinstall);
	if (sum.downgrade > 0)
		printf("Number of packages to be downgraded: %d\n",
		    sum.downgrade);
	if (sum.fetch > 0)
		printf("Number of packages to be fetched: %d\n", sum.fetch);
	if (sum.group_install > 0)
		printf("Number of groups to be installed: %d\n", sum.group_install);
	if (sum.group_upgrade > 0)
		printf("Number of groups to be upgraded: %d\n", sum.group_upgrade);
	/* Add an extra line before the size output. */
	if (bytes_change > limbytes || dlsize)
		putchar('\n');

	if (bytes_change > limbytes) {
		if (oldsize > newsize) {
			humanize_number(size, sizeof(size), oldsize - newsize, "B",
			    HN_AUTOSCALE, HN_IEC_PREFIXES);
			printf("The operation will free %s.\n", size);
		} else if (newsize > oldsize) {
			humanize_number(size, sizeof(size), newsize - oldsize, "B",
			    HN_AUTOSCALE, HN_IEC_PREFIXES);
			printf("The process will require %s more space.\n", size);
		}
	}

	if (dlsize > 0) {
		humanize_number(size, sizeof(size), dlsize, "B",
		    HN_AUTOSCALE, HN_IEC_PREFIXES);
		printf("%s to be downloaded.\n", size);
	}

	return (displayed);
}

int
print_pkg(struct pkg *p, void *ctx)
{
	int *counter = ctx;
	bool locked, vital;
	const char *lock_type;

	pkg_get(p, PKG_ATTR_LOCKED, &locked);
	pkg_get(p, PKG_ATTR_VITAL, &vital);
	if (locked) {
		if (vital)
			lock_type = "vital and locked";
		else
			lock_type = "locked";
	} else {
		if (vital)
			lock_type = "vital";
		else
			lock_type = "invalid, report an issue";
	}
	pkg_printf("\t%n (%S)\n", p, lock_type);
	(*counter)++;

	return 0;
}

void
print_repository(struct pkg_repo *repo, bool pad)
{
	const char	*mirror, *sig;

	switch (pkg_repo_mirror_type(repo)) {
		case SRV:
			mirror = "SRV";
			break;
		case HTTP:
			mirror = "HTTP";
			break;
		case NOMIRROR:
			mirror = "NONE";
			break;
		default:
			mirror = "-unknown-";
			break;
	}
	switch (pkg_repo_signature_type(repo)) {
		case SIG_PUBKEY:
			sig = "PUBKEY";
			break;
		case SIG_FINGERPRINT:
			sig = "FINGERPRINTS";
			break;
		case SIG_NONE:
			sig = "NONE";
			break;
		default:
			sig = "-unknown-";
			break;
	}

	printf("%s%s: { \n    %-16s: \"%s\",\n    %-16s: %s,\n"
			"    %-16s: %d",
			pad ? "  " : "",
			pkg_repo_name(repo),
			"url", pkg_repo_url(repo),
			"enabled", pkg_repo_enabled(repo) ? "yes" : "no",
			"priority", pkg_repo_priority(repo));

	if (pkg_repo_mirror_type(repo) != NOMIRROR)
		printf(",\n    %-16s: \"%s\"",
				"mirror_type", mirror);
	if (pkg_repo_signature_type(repo) != SIG_NONE)
		printf(",\n    %-16s: \"%s\"",
				"signature_type", sig);
	if (pkg_repo_fingerprints(repo) != NULL)
		printf(",\n    %-16s: \"%s\"",
				"fingerprints", pkg_repo_fingerprints(repo));
	if (pkg_repo_key(repo) != NULL)
		printf(",\n    %-16s: \"%s\"",
				"pubkey", pkg_repo_key(repo));
	if (pkg_repo_ip_version(repo) != 0)
		printf(",\n    %-16s: %u",
				"ip_version", pkg_repo_ip_version(repo));
	printf("\n  }\n");
}
