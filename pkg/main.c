/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
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
#include <sys/jail.h>

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <jail.h>

#include <pkg.h>

#include "pkgcli.h"

#define PKGVERSION "1.0-beta10"
#ifndef GITHASH
#define GITHASH ""
#endif

static void usage(void);
static void usage_help(void);
static int exec_help(int, char **);
bool quiet = false;

static struct commands {
	const char * const name;
	const char * const desc;
	int (*exec)(int argc, char **argv);
	void (* const usage)(void);
} cmd[] = {
	{ "add", "Registers a package and installs it on the system", exec_add, usage_add},
	{ "audit", "Reports vulnerable packages", exec_audit, usage_audit},
	{ "autoremove", "Removes orphan packages", exec_autoremove, usage_autoremove},
	{ "backup", "Backup and restore the local package database", exec_backup, usage_backup},
	{ "check", "Check for missing dependencies and database consistency", exec_check, usage_check},
	{ "clean", "Cleans old packages from the cache", exec_clean, usage_clean},
	{ "create", "Creates software package distributions", exec_create, usage_create},
	{ "delete", "Deletes packages from the database and the system", exec_delete, usage_delete},
	{ "fetch", "Fetches packages from a remote repository", exec_fetch, usage_fetch},
	{ "help", "Displays help information", exec_help, usage_help},
	{ "info", "Displays information for installed packages", exec_info, usage_info},
	{ "install", "Installs packages from remote package repositories", exec_install, usage_install},
	{ "query", "Query information for installed packages", exec_query, usage_query},
	{ "search", "Performs a search in remote package repositories", exec_search, usage_search},
	{ "set", "Modify local database informations", exec_set, usage_set},
	{ "register", "Registers a package into the local package database", exec_register, usage_register},
	{ "repo", "Creates a package database repository", exec_repo, usage_repo},
	{ "shlib", "Displays which package links against a specific shared library", exec_shlib, usage_shlib},
	{ "update", "Updates remote package repository databases", exec_update, usage_update},
	{ "updating", "Displays UPDATING information for a package", exec_updating, usage_updating},
	{ "upgrade", "Performs upgrades of package software distributions", exec_upgrade, usage_upgrade},
	{ "version", "Summarize installed versions of packages", exec_version, usage_version},
	{ "which", "Displays which package installed a specific file", exec_which, usage_which},
};

const unsigned int cmd_len = (sizeof(cmd)/sizeof(cmd[0]));

static void
usage(void)
{
	fprintf(stderr, "usage: pkg [-v] [-d] [-j <jail name or id>|-c <chroot path>] <command> [<args>]\n\n");
	fprintf(stderr, "Global options supported:\n");
	fprintf(stderr, "\t%-15s%s\n", "-d", "Increment debug level");
	fprintf(stderr, "\t%-15s%s\n", "-j", "Execute pkg(1) inside a jail(8)");
	fprintf(stderr, "\t%-15s%s\n", "-c", "Execute pkg(1) inside a chroot(8)");
	fprintf(stderr, "\t%-15s%s\n\n", "-v", "Display pkg(1) version");
	fprintf(stderr, "Commands supported:\n");

	for (unsigned int i = 0; i < cmd_len; i++) 
		fprintf(stderr, "\t%-15s%s\n", cmd[i].name, cmd[i].desc);

	fprintf(stderr, "\nFor more information on the different commands"
			" see 'pkg help <command>'.\n");

	exit(EX_USAGE);
}

static void
usage_help(void)
{
	fprintf(stderr, "usage: pkg help <command>\n\n");
	fprintf(stderr, "Where <command> can be:\n");

	for (unsigned int i = 0; i < cmd_len; i++)
		fprintf(stderr, "\t%s\n", cmd[i].name);
}

static int
exec_help(int argc, char **argv)
{
	char *manpage;

	if ((argc != 2) || (strcmp("help", argv[1]) == 0)) {
		usage_help();
		return(EX_USAGE);
	}

	for (unsigned int i = 0; i < cmd_len; i++) {
		if (strcmp(cmd[i].name, argv[1]) == 0) {
			if (asprintf(&manpage, "/usr/bin/man pkg-%s", cmd[i].name) == -1)
				errx(1, "cannot allocate memory");

			system(manpage);
			free(manpage);

			return (0);
		}
	}

	/* Command name not found */
	warnx("'%s' is not a valid command.\n", argv[1]);
	
	fprintf(stderr, "See 'pkg help' for more information on the commands.\n");

	return (EX_USAGE);
}

int
main(int argc, char **argv)
{
	unsigned int i;
	struct commands *command = NULL;
	unsigned int ambiguous = 0;
	const char *chroot_path = NULL;
	int jid;
	const char *jail_str = NULL;
	size_t len;
	signed char ch;
	int debug = 0;
	int version = 0;
	int ret = EXIT_SUCCESS;
	const char *buf = NULL;
	bool b;
	struct pkg_config_kv *kv = NULL;

	if (argc < 2)
		usage();

	while ((ch = getopt(argc, argv, "dj:c:vq")) != -1) {
		switch(ch) {
			case 'd':
				debug++;
				break;
			case 'c':
				chroot_path = optarg;
				break;
			case 'j':
				jail_str = optarg;
				break;
			case 'v':
				version++;
				break; /* NOT REACHED */
			default:
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (version == 1) {
		printf(PKGVERSION""GITHASH"\n");
		exit(EXIT_SUCCESS);
	}
	if (argc == 0 && version == 0)
		usage();

	pkg_event_register(&event_callback, &debug);

	/* reset getopt for the next call */
	optreset = 1;
	optind = 1;

	if (jail_str != NULL && chroot_path != NULL) {
		fprintf(stderr, "-j and -c cannot be used at the same time\n");
		usage();
	}

	if (chroot_path != NULL)
		if (chroot(chroot_path) == -1)
			errx(EX_SOFTWARE, "chroot failed");

	if (jail_str != NULL) {
		jid = jail_getid(jail_str);
		if (jid < 0)
			errx(1, "%s", jail_errmsg);

		if (jail_attach(jid) == -1)
			err(1, "jail_attach(%s)", jail_str);
	}

	if (jail_str != NULL || chroot_path != NULL)
		if (chdir("/") == -1)
			errx(EX_SOFTWARE, "chdir() failed");

	if (pkg_init(NULL) != EPKG_OK)
		errx(EX_SOFTWARE, "can not parse configuration file");

	if (version > 1) {
		printf("version: "PKGVERSION""GITHASH"\n");
		pkg_config_string(PKG_CONFIG_ABI, &buf);
		printf("abi: %s\n", buf);
		pkg_config_string(PKG_CONFIG_DBDIR, &buf);
		printf("db dir: %s\n", buf);
		pkg_config_string(PKG_CONFIG_CACHEDIR, &buf);
		printf("cache dir: %s\n", buf);
		pkg_config_string(PKG_CONFIG_PORTSDIR, &buf);
		printf("ports dir: %s\n", buf);
		pkg_config_bool(PKG_CONFIG_SYSLOG, &b);
		printf("Log into syslog: %s\n", b ? "yes": "no");
		pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &b);
		printf("Assume always yes: %s\n", b ? "yes" : "no");
		if (version > 2) {
			pkg_config_bool(PKG_CONFIG_HANDLE_RC_SCRIPTS, &b);
			printf("Handle rc scripts: %s\n", b ? "yes" : "no");
			pkg_config_bool(PKG_CONFIG_SHLIBS, &b);
			printf("Track shlibs: %s\n", b ? "yes" : "no");
			pkg_config_bool(PKG_CONFIG_AUTODEPS, &b);
			printf("Automatic depdency tracking: %s\n", b ? "yes": "no");
			pkg_config_string(PKG_CONFIG_PLIST_KEYWORDS_DIR, &buf);
			printf("Custom keywords directory: %s\n", buf ? buf : "none");
		}
		pkg_config_bool(PKG_CONFIG_MULTIREPOS, &b);
		if (b) {
			printf("Repositories:\n");
			while (pkg_config_list(PKG_CONFIG_REPOS, &kv) == EPKG_OK) {
				printf("             - %s: %s\n", pkg_config_kv_get(kv, PKG_CONFIG_KV_KEY),
				    pkg_config_kv_get(kv, PKG_CONFIG_KV_VALUE));
			}
		} else {
			pkg_config_string(PKG_CONFIG_REPO, &buf);
			printf("Repository: %s\n", buf ? buf : "none");
		}
		pkg_shutdown();
		exit(EXIT_SUCCESS);
	}

	len = strlen(argv[0]);
	for (i = 0; i < cmd_len; i++) {
		if (strncmp(argv[0], cmd[i].name, len) == 0) {
			/* if we have the exact cmd */
			if (len == strlen(cmd[i].name)) {
				command = &cmd[i];
				ambiguous = 0;
				break;
			}

			/*
			 * we already found a partial match so `argv[0]' is
			 * an ambiguous shortcut
			 */
			ambiguous++;

			command = &cmd[i];
		}
	}

	if (command == NULL) {
		pkg_shutdown();
		usage();
		return (ret); /* Not reached but makes scanbuild happy */
	}

	if (ambiguous <= 1) {
		assert(command->exec != NULL);
		ret = command->exec(argc, argv);
	} else {
		warnx("'%s' is not a valid command.\n", argv[0]);

		fprintf(stderr, "See 'pkg help' for more information on the commands.\n\n");
		fprintf(stderr, "Command '%s' could be one of the following:\n", argv[0]);

		for (i = 0; i < cmd_len; i++)
			if (strncmp(argv[0], cmd[i].name, len) == 0)
				fprintf(stderr, "\t%s\n",cmd[i].name);
	}

	pkg_shutdown();
	return (ret);
}

