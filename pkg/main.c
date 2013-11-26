/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/sbuf.h>

#include <assert.h>
#include <err.h>
#include <histedit.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#ifndef NO_LIBJAIL
#include <jail.h>
#endif

#include <pkg.h>

#include "pkgcli.h"

static void usage(const char *, const char *);
static void usage_help(void);
static int exec_help(int, char **);
bool quiet = false;
static char **cmdargv;
bool newpkgversion = false;

static struct commands {
	const char * const name;
	const char * const desc;
	int (*exec)(int argc, char **argv);
	void (* const usage)(void);
} cmd[] = {
	{ "add", "Registers a package and installs it on the system", exec_add, usage_add},
	{ "annotate", "Add, modify or delete tag-value style annotations on packages", exec_annotate, usage_annotate},
	{ "audit", "Reports vulnerable packages", exec_audit, usage_audit},
	{ "autoremove", "Removes orphan packages", exec_autoremove, usage_autoremove},
	{ "backup", "Backs-up and restores the local package database", exec_backup, usage_backup},
	{ "check", "Checks for missing dependencies and database consistency", exec_check, usage_check},
	{ "clean", "Cleans old packages from the cache", exec_clean, usage_clean},
	{ "config", "Display the value of the configuration options", exec_config, usage_config},
	{ "convert", "Convert database from/to pkgng", exec_convert, usage_convert},
	{ "create", "Creates software package distributions", exec_create, usage_create},
	{ "delete", "Deletes packages from the database and the system", exec_delete, usage_delete},
	{ "fetch", "Fetches packages from a remote repository", exec_fetch, usage_fetch},
	{ "help", "Displays help information", exec_help, usage_help},
	{ "info", "Displays information about installed packages", exec_info, usage_info},
	{ "install", "Installs packages from remote package repositories", exec_install, usage_install},
	{ "lock", "Locks package against modifications or deletion", exec_lock, usage_lock},
	{ "plugins", "Manages plugins and displays information about plugins", exec_plugins, usage_plugins},
	{ "query", "Queries information about installed packages", exec_query, usage_query},
	{ "register", "Registers a package into the local database", exec_register, usage_register},
	{ "remove", "Deletes packages from the database and the system", exec_delete, usage_delete},
	{ "repo", "Creates a package repository catalogue", exec_repo, usage_repo},
	{ "rquery", "Queries information in repository catalogues", exec_rquery, usage_rquery},
	{ "search", "Performs a search of package repository catalogues", exec_search, usage_search},
	{ "set", "Modifies information about packages in the local database", exec_set, usage_set},
	{ "ssh", "ssh packages to be used via ssh", exec_ssh, usage_ssh},
	{ "shell", "Opens a debug shell", exec_shell, usage_shell},
	{ "shlib", "Displays which packages link against a specific shared library", exec_shlib, usage_shlib},
	{ "stats", "Displays package database statistics", exec_stats, usage_stats},
	{ "unlock", "Unlocks a package, allowing modification or deletion", exec_unlock, usage_lock},
	{ "update", "Updates package repository catalogues", exec_update, usage_update},
	{ "updating", "Displays UPDATING information for a package", exec_updating, usage_updating},
	{ "upgrade", "Performs upgrades of packaged software distributions", exec_upgrade, usage_upgrade},
	{ "version", "Displays the versions of installed packages", exec_version, usage_version},
	{ "which", "Displays which package installed a specific file", exec_which, usage_which},
};

static const unsigned int cmd_len = sizeof(cmd) / sizeof(cmd[0]);

static STAILQ_HEAD(, plugcmd) plugins = STAILQ_HEAD_INITIALIZER(plugins);
struct plugcmd {
	const char *name;
	const char *desc;
	int (*exec)(int argc, char **argv);
	STAILQ_ENTRY(plugcmd) next;
};

typedef int (register_cmd)(int idx, const char **name, const char **desc, int (**exec)(int argc, char **argv));
typedef int (nb_cmd)(void);

static void
show_command_names(void)
{
	unsigned	i;

	for(i = 0; i < cmd_len; i++)
		printf("%s\n", cmd[i].name);

	return;
}

static void
usage(const char *conffile, const char *reposdir)
{
	struct plugcmd *c;
	bool plugins_enabled = false;
	unsigned int i;

#ifndef NO_LIBJAIL
 	fprintf(stderr, "Usage: pkg [-v] [-d] [-l] [-N] [-j <jail name or id>|-c <chroot path>] [-C <configuration file>] [-R <repo config dir>] <command> [<args>]\n\n");
#else
	fprintf(stderr, "Usage: pkg [-v] [-d] [-l] [-N] [-c <chroot path>] [-C <configuration file>] [-R <repo config dir>] <command> [<args>]\n\n");
#endif
	fprintf(stderr, "Global options supported:\n");
	fprintf(stderr, "\t%-15s%s\n", "-d", "Increment debug level");
#ifndef NO_LIBJAIL
	fprintf(stderr, "\t%-15s%s\n", "-j", "Execute pkg(8) inside a jail(8)");
#endif
	fprintf(stderr, "\t%-15s%s\n", "-c", "Execute pkg(8) inside a chroot(8)");
	fprintf(stderr, "\t%-15s%s\n", "-C", "Use the specified configuration file");
	fprintf(stderr, "\t%-15s%s\n", "-R", "Directory to search for individual repository configurations");
	fprintf(stderr, "\t%-15s%s\n", "-l", "List available commands and exit");
	fprintf(stderr, "\t%-15s%s\n", "-v", "Display pkg(8) version");
	fprintf(stderr, "\t%-15s%s\n\n", "-N", "Test if pkg(8) is activated and avoid auto-activation");
	fprintf(stderr, "Commands supported:\n");

	for (i = 0; i < cmd_len; i++)
		fprintf(stderr, "\t%-15s%s\n", cmd[i].name, cmd[i].desc);

	if (!pkg_initialized() && pkg_init(conffile, reposdir) != EPKG_OK)
		errx(EX_SOFTWARE, "Cannot parse configuration file!");

	pkg_config_bool(PKG_CONFIG_ENABLE_PLUGINS, &plugins_enabled);

	if (plugins_enabled) {
		if (pkg_plugins_init() != EPKG_OK)
			errx(EX_SOFTWARE, "Plugins cannot be loaded");

		printf("\nCommands provided by plugins:\n");

		STAILQ_FOREACH(c, &plugins, next)
			fprintf(stderr, "\t%-15s%s\n", c->name, c->desc);
	}

	fprintf(stderr, "\nFor more information on the different commands"
			" see 'pkg help <command>'.\n");

	exit(EX_USAGE);
}

static void
usage_help(void)
{
	usage(NULL, NULL);
}

static int
exec_help(int argc, char **argv)
{
	char *manpage;
	bool plugins_enabled = false;
	struct plugcmd *c;
	unsigned int i;

	if ((argc != 2) || (strcmp("help", argv[1]) == 0)) {
		usage_help();
		return(EX_USAGE);
	}

	for (i = 0; i < cmd_len; i++) {
		if (strcmp(cmd[i].name, argv[1]) == 0) {
			if (asprintf(&manpage, "/usr/bin/man pkg-%s", cmd[i].name) == -1)
				errx(EX_SOFTWARE, "cannot allocate memory");

			system(manpage);
			free(manpage);

			return (0);
		}
	}

	pkg_config_bool(PKG_CONFIG_ENABLE_PLUGINS, &plugins_enabled);

	if (plugins_enabled) {
		STAILQ_FOREACH(c, &plugins, next) {
			if (strcmp(c->name, argv[1]) == 0) {
				if (asprintf(&manpage, "/usr/bin/man pkg-%s", c->name) == -1)
					errx(EX_SOFTWARE, "cannot allocate memory");

				system(manpage);
				free(manpage);

				return (0);
			}
		}
	}

	if (strcmp(argv[1], "pkg") == 0) {
		system("/usr/bin/man 8 pkg");
		return (0);
	} else if (strcmp(argv[1], "pkg.conf") == 0) {
		system("/usr/bin/man 5 pkg.conf");
		return (0);
	}

	/* Command name not found */
	warnx("'%s' is not a valid command.\n", argv[1]);

	fprintf(stderr, "See 'pkg help' for more information on the commands.\n");

	return (EX_USAGE);
}

static void
show_config_info(int version)
{
	struct pkg_config	*conf = NULL;
	struct pkg_config_value	*list = NULL;
	struct pkg_config_kv	*kv = NULL;
	const char		*configname;
	const char		*buf = NULL;
	int			 cout;
	int64_t			 integer;
	bool			 b;

	assert(version > 1);

	while (pkg_configs(&conf) == EPKG_OK) {
		configname = pkg_config_name(conf);

		switch (pkg_config_type(conf)) {
		case PKG_CONFIG_STRING:
			pkg_config_string(pkg_config_id(conf), &buf);
			cout = printf("%-24s: %s", configname,
			    buf == NULL ? "" : buf);

			if (version > 2) {
				pkg_config_desc(pkg_config_id(conf), &buf);
				if (buf != NULL) {
					cout = (cout >= 48 ? 1 : 48 - cout);
					printf("%*s/* %s */", cout, "", buf);
				}
			}
			printf("\n");
			break;
		case PKG_CONFIG_BOOL:
			pkg_config_bool(pkg_config_id(conf), &b);
			cout = printf("%-24s: %s", configname, b ? "yes": "no");

			if (version > 2) {
				pkg_config_desc(pkg_config_id(conf), &buf);
				if (buf != NULL) {
					cout = (cout >= 48 ? 1 : 48 - cout);
					printf("%*s/* %s */", cout, "", buf);
				}
			}
			printf("\n");
			break;
		case PKG_CONFIG_INTEGER:
			pkg_config_int64(pkg_config_id(conf), &integer);
			cout = printf("%-24s: %"PRId64, configname, integer);

			if (version > 2) {
				pkg_config_desc(pkg_config_id(conf), &buf);
				if (buf != NULL) {
					cout = (cout >= 48 ? 1 : 48 - cout);
					printf("%*s/* %s */", cout, "", buf);
				}
			}
			printf("\n");
			break;
		case PKG_CONFIG_KVLIST:
			cout = printf("%-24s: {", configname);

			if (version > 2) {
				pkg_config_desc(pkg_config_id(conf), &buf);
				if (buf != NULL) {
					cout = (cout >= 48 ? 1 : 48 - cout);
					printf("%*s/* %s */", cout, "", buf);
				}
			}
			printf("\n");

			kv = NULL;
			while (pkg_config_kvlist(pkg_config_id(conf), &kv)
			       == EPKG_OK) {
				printf("  %s: %s,\n",
				    pkg_config_kv_get(kv, PKG_CONFIG_KV_KEY),
				    pkg_config_kv_get(kv, PKG_CONFIG_KV_VALUE));
			}
			printf("}\n");
			break;
		case PKG_CONFIG_LIST:
			cout = printf("%-24s: [", configname);

			if (version > 2) {
				pkg_config_desc(pkg_config_id(conf), &buf);
				if (buf != NULL) {
					cout = (cout >= 48 ? 1 : 48 - cout);
					printf("%*s/* %s */", cout, "", buf);
				}
			}
			printf("\n");

			list = NULL;
			while (pkg_config_list(pkg_config_id(conf), &list)
			       == EPKG_OK) {
				printf("  %-s,\n", pkg_config_value(list));
			}
			printf("]\n");
			break;
		}
	}
}

static void
show_plugin_info(void)
{
	struct pkg_plugin	*p = NULL;
	struct pkg_config	*conf = NULL;
	struct pkg_config_value	*list = NULL;
	struct pkg_config_kv	*kv = NULL;
	const char		*configname;
	const char		*buf;
	int64_t			 integer;
	bool			 b;

	while (pkg_plugins(&p) == EPKG_OK) {
		conf = NULL;
		printf("Configuration for plugin: %s\n",
		    pkg_plugin_get(p, PKG_PLUGIN_NAME));

		while (pkg_plugin_confs(p, &conf) == EPKG_OK) {
			configname = pkg_config_name(conf);

			switch (pkg_config_type(conf)) {
			case PKG_CONFIG_STRING:
				pkg_plugin_conf_string(p, pkg_config_id(conf),
				    &buf);
				if (buf == NULL)
					printf("\t%16s:\n", configname);
				else
					printf("\t%16s: %s\n", configname, buf);
				break;
			case PKG_CONFIG_BOOL:
				pkg_plugin_conf_bool(p, pkg_config_id(conf),
				    &b);
				printf("\t%16s: %s\n", configname,
			            b ? "yes": "no");
				break;
			case PKG_CONFIG_INTEGER:
				pkg_plugin_conf_integer(p, pkg_config_id(conf),
				    &integer);
				printf("\t%16s: %"PRId64"\n", configname,
                                    integer);
				break;
			case PKG_CONFIG_KVLIST:
				printf("\t%16s:\n", configname);
				kv = NULL;
				while (pkg_plugin_conf_kvlist(p,
                                    pkg_config_id(conf), &kv) == EPKG_OK) {
					printf("\t\t- %8s: %s\n",
					    pkg_config_kv_get(kv,
					        PKG_CONFIG_KV_KEY),
					    pkg_config_kv_get(kv,
						PKG_CONFIG_KV_VALUE));
				}
				break;
			case PKG_CONFIG_LIST:
				printf("\t%16s:\n", configname);

				list = NULL;
				while (pkg_plugin_conf_list(p,
			            pkg_config_id(conf), &list) == EPKG_OK) {
					printf("\t\t- %8s\n",
					    pkg_config_value(list));
				}
				break;
			}
		}
	}
}

static void
show_repository_info(void)
{
	const char	*mirror, *sig;
	struct pkg_repo	*repo = NULL;

	printf("\nRepositories:\n");
	while (pkg_repos(&repo) == EPKG_OK) {
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

		printf("  %s: { \n    %-16s: \"%s\",\n    %-16s: %s",
		    pkg_repo_ident(repo),
                    "url", pkg_repo_url(repo),
		    "enabled", pkg_repo_enabled(repo) ? "yes" : "no");
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
		printf("\n  }\n");
	}
}

static void
show_version_info(int version)
{
	if (version > 1)
		printf("%-24s: ", "Version");

#ifndef GITHASH
	printf(PKG_PORTVERSION"\n");
#else
	printf(PKG_PORTVERSION"-"GITHASH"\n");
#endif

	if (version == 1)
		exit(EX_OK);

	show_config_info(version);
	show_plugin_info();
	show_repository_info();
	
	exit(EX_OK);
	/* NOTREACHED */
}

static void
do_activation_test(int argc)
{
	int	count;

	/* Test to see if pkg(8) has been activated.  Exit with an
	   error code if not.  Can be combined with -c and -j to test
	   if pkg is activated in chroot or jail. If there are no
	   other arguments, and pkg(8) has been activated, show how
	   many packages have been installed. */

	switch (pkg_status(&count)) {
	case PKG_STATUS_UNINSTALLED: /* This case shouldn't ever happen... */
		errx(EX_UNAVAILABLE, "can't execute " PKG_EXEC_NAME
		    " or " PKG_STATIC_NAME "\n");
		/* NOTREACHED */
	case PKG_STATUS_NODB:
		errx(EX_UNAVAILABLE, "package database non-existent");
		/* NOTREACHED */
	case PKG_STATUS_NOPACKAGES:
		errx(EX_UNAVAILABLE, "no packages registered");
		/* NOTREACHED */
	case PKG_STATUS_ACTIVE:
		if (argc == 0) {
			warnx("%d packages installed", count);
			exit(EX_OK);
		}
		break;
	}
	return;
}

int
main(int argc, char **argv)
{
	unsigned int i;
	struct commands *command = NULL;
	unsigned int ambiguous = 0;
	const char *chroot_path = NULL;
#ifndef NO_LIBJAIL
	int jid;
#endif
	const char *jail_str = NULL;
	size_t len;
	signed char ch;
	int debug = 0;
	int version = 0;
	int ret = EX_OK;
	bool plugins_enabled = false;
	bool plugin_found = false;
	bool show_commands = false;
	bool activation_test = false;
	struct plugcmd *c;
	const char *conffile = NULL;
	const char *reposdir = NULL;
	struct pkg_config_kv *alias = NULL;
	const char *alias_value;
	char **newargv;
	int newargc;
	Tokenizer *t = NULL;
	struct sbuf *newcmd;
	int j;

	/* Set stdout unbuffered */
	setvbuf(stdout, NULL, _IONBF, 0);

	cmdargv = argv;

	if (argc < 2)
		usage(NULL, NULL);

#ifndef NO_LIBJAIL
	while ((ch = getopt(argc, argv, "dj:c:C:R:lNvq")) != -1) {
#else
	while ((ch = getopt(argc, argv, "d:c:C:R:lNvq")) != -1) {
#endif
		switch (ch) {
		case 'd':
			debug++;
			break;
		case 'c':
			chroot_path = optarg;
			break;
		case 'C':
			conffile = optarg;
			break;
		case 'R':
			reposdir = optarg;
			break;
#ifndef NO_LIBJAIL
		case 'j':
			jail_str = optarg;
			break;
#endif
		case 'l':
			show_commands = true;
			break;
		case 'N':
			activation_test = true;
			break;
		case 'v':
			version++;
			break;
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (version == 1)
		show_version_info(version);

	if (show_commands && version == 0) {
		show_command_names();
		exit(EX_OK);
	}

	if (argc == 0 && version == 0 && !activation_test)
		usage(conffile, reposdir);

	umask(022);
	pkg_event_register(&event_callback, &debug);

	/* reset getopt for the next call */
	optreset = 1;
	optind = 1;

	if (jail_str != NULL && chroot_path != NULL) {
		fprintf(stderr, "-j and -c cannot be used at the same time!\n");
		usage(conffile, reposdir);
	}

	if (chroot_path != NULL)
		if (chroot(chroot_path) == -1)
			errx(EX_SOFTWARE, "chroot failed!");

#ifndef NO_LIBJAIL
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
#endif

	if (pkg_init(conffile, reposdir) != EPKG_OK)
		errx(EX_SOFTWARE, "Cannot parse configuration file!");

	if (atexit(&pkg_shutdown) != 0)
		errx(EX_SOFTWARE, "register pkg_shutdown() to run at exit");

	pkg_config_bool(PKG_CONFIG_ENABLE_PLUGINS, &plugins_enabled);

	if (plugins_enabled) {
		struct pkg_plugin	*p = NULL;

		if (pkg_plugins_init() != EPKG_OK)
			errx(EX_SOFTWARE, "Plugins cannot be loaded");

		if (atexit(&pkg_plugins_shutdown) != 0)
			errx(EX_SOFTWARE,
                            "register pkg_plugins_shutdown() to run at exit");

		/* load commands plugins */
		while (pkg_plugins(&p) != EPKG_END) {
			int n;

			nb_cmd *ncmd = pkg_plugin_func(p, "pkg_register_cmd_count");
			register_cmd *reg = pkg_plugin_func(p, "pkg_register_cmd");
			if (reg != NULL && ncmd != NULL) {
				n = ncmd();
				for (j = 0; j < n ; j++) {
					c = malloc(sizeof(struct plugcmd));
					reg(j, &c->name, &c->desc, &c->exec);
					STAILQ_INSERT_TAIL(&plugins, c, next);
				}
			}
		}
	}

	if (version > 1)
		show_version_info(version);

	if (activation_test)
		do_activation_test(argc);

	if (argc == 1 && strcmp(argv[0], "bootstrap") == 0) {
		printf("pkg already bootstrapped\n");
		exit(EXIT_SUCCESS);
	}

	newargv = argv;
	newargc = argc;
	alias = NULL;
	while (pkg_config_kvlist(PKG_CONFIG_ALIAS, &alias) == EPKG_OK) {
		if (strcmp(argv[0], pkg_config_kv_get(alias, PKG_CONFIG_KV_KEY)) == 0) {
			if ((alias_value = pkg_config_kv_get(alias, PKG_CONFIG_KV_VALUE)) == NULL)
				continue;
			argv++;
			argc--;
			newcmd = sbuf_new_auto();
			sbuf_cat(newcmd, alias_value);
			for (j = 0; j < argc; j++) {
				if (strspn(argv[j], " \t\n") > 0)
					sbuf_printf(newcmd, " \"%s\" ", argv[j]);
				else
					sbuf_printf(newcmd, " %s ", argv[j]);
			}
			sbuf_done(newcmd);
			t = tok_init(NULL);
			/* XXX: __DECONST() workaround gcc's -Werror=cast-qual. */
			if (tok_str(t, sbuf_data(newcmd), &newargc, __DECONST(const char ***, &newargv)) != 0)
				errx(EX_CONFIG, "Invalid alias: %s", alias_value);
			sbuf_delete(newcmd);
			break;
		}
	}

	len = strlen(newargv[0]);
	for (i = 0; i < cmd_len; i++) {
		if (strncmp(newargv[0], cmd[i].name, len) == 0) {
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
		/* Check if a plugin provides the requested command */
		ret = EPKG_FATAL;
		if (plugins_enabled) {
			STAILQ_FOREACH(c, &plugins, next) {
				if (strcmp(c->name, newargv[0]) == 0) {
					plugin_found = true;
					ret = c->exec(newargc, newargv);
					break;
				}
			}
		}

		if (!plugin_found)
			usage(conffile, reposdir);

		return (ret);
	}

	if (ambiguous <= 1) {
		assert(command->exec != NULL);
		ret = command->exec(newargc, newargv);
	} else {
		warnx("'%s' is not a valid command.\n", newargv[0]);

		fprintf(stderr, "See 'pkg help' for more information on the commands.\n\n");
		fprintf(stderr, "Command '%s' could be one of the following:\n", newargv[0]);

		for (i = 0; i < cmd_len; i++)
			if (strncmp(newargv[0], cmd[i].name, len) == 0)
				fprintf(stderr, "\t%s\n",cmd[i].name);
	}

	if (alias != NULL)
		tok_end(t);

	if (ret == EX_OK && newpkgversion)
		execvp(getprogname(), cmdargv);

	return (ret);
}

