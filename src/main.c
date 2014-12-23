/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#ifdef HAVE_LIBJAIL
#include <jail.h>
#include <sys/jail.h>
#endif
#include <signal.h>

#include <pkg.h>

#include "pkgcli.h"

/* Used to define why do we show usage message to a user */
enum pkg_usage_reason {
	PKG_USAGE_ERROR,
	PKG_USAGE_UNKNOWN_COMMAND,
	PKG_USAGE_INVALID_ARGUMENTS,
	PKG_USAGE_HELP
};

static void usage(const char *, const char *, FILE *, enum pkg_usage_reason, ...);
static void usage_help(void);
static int exec_help(int, char **);

static struct commands {
	const char * const name;
	const char * const desc;
	int (*exec)(int argc, char **argv);
	void (* const usage)(void);
} cmd[] = {
	{ "add", "Compatibility interface to install a package", exec_add, usage_add},
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
	{ "install", "Installs packages from remote package repositories and local archives", exec_install, usage_install},
	{ "lock", "Locks package against modifications or deletion", exec_lock, usage_lock},
	{ "plugins", "Manages plugins and displays information about plugins", exec_plugins, usage_plugins},
	{ "query", "Queries information about installed packages", exec_query, usage_query},
	{ "register", "Registers a package into the local database", exec_register, usage_register},
	{ "remove", "Deletes packages from the database and the system", exec_delete, usage_delete},
	{ "repo", "Creates a package repository catalogue", exec_repo, usage_repo},
	{ "rquery", "Queries information in repository catalogues", exec_rquery, usage_rquery},
	{ "search", "Performs a search of package repository catalogues", exec_search, usage_search},
	{ "set", "Modifies information about packages in the local database", exec_set, usage_set},
	{ "ssh", "Package server (to be used via ssh)", exec_ssh, usage_ssh},
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

static const unsigned int cmd_len = NELEM(cmd);

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
usage(const char *conffile, const char *reposdir, FILE *out, enum pkg_usage_reason reason, ...)
{
	struct plugcmd *c;
	bool plugins_enabled = false;
	unsigned int i;
	const char *arg;
	va_list vp;

	if (reason == PKG_USAGE_UNKNOWN_COMMAND) {
		va_start(vp, reason);
		arg = va_arg(vp, const char *);
		va_end(vp);
		fprintf(out, "pkg: unknown command: %s\n", arg);
		goto out;
	}
	else if (reason == PKG_USAGE_INVALID_ARGUMENTS) {
		va_start(vp, reason);
		arg = va_arg(vp, const char *);
		va_end(vp);
		fprintf(out, "pkg: %s\n", arg);
	}

#ifdef HAVE_LIBJAIL
 	fprintf(out, "Usage: pkg [-v] [-d] [-l] [-N] [-j <jail name or id>|-c <chroot path>] [-C <configuration file>] [-R <repo config dir>] [-o var=value] [-4|-6] <command> [<args>]\n");
#else
	fprintf(out, "Usage: pkg [-v] [-d] [-l] [-N] [-c <chroot path>] [-C <configuration file>] [-R <repo config dir>] [-o var=value] [-4|-6] <command> [<args>]\n");
#endif
	if (reason == PKG_USAGE_HELP) {
		fprintf(out, "Global options supported:\n");
		fprintf(out, "\t%-15s%s\n", "-d", "Increment debug level");
#ifdef HAVE_LIBJAIL
		fprintf(out, "\t%-15s%s\n", "-j", "Execute pkg(8) inside a jail(8)");
#endif
		fprintf(out, "\t%-15s%s\n", "-c", "Execute pkg(8) inside a chroot(8)");
		fprintf(out, "\t%-15s%s\n", "-C", "Use the specified configuration file");
		fprintf(out, "\t%-15s%s\n", "-R", "Directory to search for individual repository configurations");
		fprintf(out, "\t%-15s%s\n", "-l", "List available commands and exit");
		fprintf(out, "\t%-15s%s\n", "-v", "Display pkg(8) version");
		fprintf(out, "\t%-15s%s\n\n", "-N", "Test if pkg(8) is activated and avoid auto-activation");
		fprintf(out, "\t%-15s%s\n\n", "-o", "Override configuration option from the command line");
		fprintf(out, "\t%-15s%s\n", "-4", "Only use IPv4");
		fprintf(out, "\t%-15s%s\n", "-6", "Only use IPv6");
		fprintf(out, "Commands supported:\n");

		for (i = 0; i < cmd_len; i++)
			fprintf(out, "\t%-15s%s\n", cmd[i].name, cmd[i].desc);

		if (!pkg_initialized() && pkg_ini(conffile, reposdir, 0) != EPKG_OK)
			errx(EX_SOFTWARE, "Cannot parse configuration file!");

		plugins_enabled = pkg_object_bool(pkg_config_get("PKG_ENABLE_PLUGINS"));

		if (plugins_enabled) {
			if (pkg_plugins_init() != EPKG_OK)
				errx(EX_SOFTWARE, "Plugins cannot be loaded");

			fprintf(out, "\nCommands provided by plugins:\n");

			STAILQ_FOREACH(c, &plugins, next)
			fprintf(out, "\t%-15s%s\n", c->name, c->desc);
		}
		fprintf(out, "\nFor more information on the different commands"
					" see 'pkg help <command>'.\n");
		exit(EXIT_SUCCESS);
	}

out:
	fprintf(out, "\nFor more information on available commands and options see 'pkg help'.\n");
	exit(EX_USAGE);
}

static void
usage_help(void)
{
	usage(NULL, NULL, stdout, PKG_USAGE_HELP);
}

static int
exec_help(int argc, char **argv)
{
	char *manpage;
	bool plugins_enabled = false;
	struct plugcmd *c;
	unsigned int i;
	const pkg_object *all_aliases;
	const pkg_object *alias;
	pkg_iter it = NULL;

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

	plugins_enabled = pkg_object_bool(pkg_config_get("PKG_ENABLE_PLUGINS"));

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

	/* Try aliases */
	all_aliases = pkg_config_get("ALIAS");
	while ((alias = pkg_object_iterate(all_aliases, &it))) {
		if (strcmp(argv[1], pkg_object_key(alias)) == 0) {
			printf("`%s` is an alias to `%s`\n", argv[1], pkg_object_string(alias));
			return (0);
		}
	}

	/* Command name not found */
	warnx("'%s' is not a valid command.\n", argv[1]);

	fprintf(stderr, "See 'pkg help' for more information on the commands.\n");

	return (EX_USAGE);
}

static void
show_plugin_info(void)
{
	const pkg_object	*conf;
	struct pkg_plugin	*p = NULL;

	while (pkg_plugins(&p) == EPKG_OK) {
		conf = pkg_plugin_conf(p);
		printf("Configuration for plugin: %s\n",
		    pkg_plugin_get(p, PKG_PLUGIN_NAME));

		printf("%s\n", pkg_object_dump(conf));
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
		    pkg_repo_name(repo),
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

	printf(PKG_PORTVERSION""GITHASH"\n");

	if (version == 1)
		exit(EX_OK);

	printf("%s\n", pkg_config_dump());
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

static void
export_arg_option (char *arg)
{
	char *eqp;
	const char *opt;

	if ((eqp = strchr(arg, '=')) != NULL) {
		*eqp = '\0';

		if ((opt = getenv (arg)) != NULL) {
			warnx("option %s is defined in the environment to '%s' but command line "
					"option redefines it", arg, opt);
			setenv(arg, eqp + 1, 1);
		}
		else {
			setenv(arg, eqp + 1, 0);
		}

		*eqp = '=';
	}
}

static void
start_process_worker(char *const *save_argv)
{
	int	ret = EX_OK;
	int	status;
	pid_t	child_pid;

	/* Fork off a child process to do the actual package work.
	 * The child may be jailed or chrooted.  If a restart is required
	 * (eg. pkg(8) inself was upgraded) the child can exit with
	 * 'EX_NEEDRESTART' and the same forking process will be
	 * replayed.  This function returns control in the child
	 * process only. */

	while (1) {
		child_pid = fork();

		if (child_pid == 0) {
			/* Load the new Pkg image */
			if (ret == EX_NEEDRESTART)
				execvp(getprogname(), save_argv);
			return;
		} else {
			if (child_pid == -1)
				err(EX_OSERR, "Failed to fork worker process");

			while (waitpid(child_pid, &status, 0) == -1) {
				if (errno != EINTR)
					err(EX_OSERR, "Child process pid=%d", (int)child_pid);
			}

			ret = WEXITSTATUS(status);

			if (WIFEXITED(status) && ret != EX_NEEDRESTART)
				break;
			if (WIFSIGNALED(status)) {
				/* Process got some terminating signal, hence stop the loop */
				fprintf(stderr, "Child process pid=%d terminated abnormally: %s\n",
						(int)child_pid, strsignal (WTERMSIG(status)));
				ret = -(WTERMSIG(status));
				break;
			}
		}
	}

	exit(ret);
	/* NOTREACHED */
}

static int
expand_aliases(int argc, char ***argv)
{
	pkg_iter		  it = NULL;
	const pkg_object	 *all_aliases;
	const pkg_object	 *alias;
	const char		 *alias_value;
	void			 *buf;
	char			**oldargv = *argv;
	char			**newargv;
	char			 *args;
	int			  newargc; 
	int			  spaces;
	int			  i;
	size_t			  veclen;
	size_t			  arglen;
	bool			  matched = false;

	all_aliases = pkg_config_get("ALIAS");

	while ((alias = pkg_object_iterate(all_aliases, &it))) {
		if (strcmp(oldargv[0], pkg_object_key(alias)) == 0) {
			matched = true;
			break;
		}
	}

	if (!matched || (alias_value = pkg_object_string(alias)) == NULL)
		return (argc);	/* Nothing to do */

	/* Estimate how many args alias_value will split into by
	 * counting the number of whitespace characters in it. This
	 * will be at minimum one less than the final argc. We'll be
	 * consuming one of the orginal argv, so that balances
	 * out. */ 

	spaces = pkg_utils_count_spaces(alias_value);
	arglen = strlen(alias_value) + 1;
	veclen = sizeof(char *) * (spaces + argc + 1);
	buf = malloc(veclen + arglen);
	if (buf == NULL)
		err(EX_OSERR, "expanding aliases");

	newargv = (char **) buf;
	args = (char *) (buf + veclen);
	strlcpy(args, alias_value, arglen);

	newargc = 0;
	while(args != NULL) {
		newargv[newargc++] = pkg_utils_tokenize(&args);
	}
	for (i = 1; i < argc; i++) {
		newargv[newargc++] = oldargv[i];
	}
	newargv[newargc] = NULL;

	*argv = newargv;
	return (newargc);
}

int
main(int argc, char **argv)
{
	unsigned int	  i;
	struct commands	 *command = NULL;
	unsigned int	  ambiguous = 0;
	const char	 *chroot_path = NULL;
#ifdef HAVE_LIBJAIL
	int		  jid;
#endif
	const char	 *jail_str = NULL;
	size_t		  len;
	signed char	  ch;
	int64_t		  debug = 0;
	int		  version = 0;
	int		  ret = EX_OK;
	bool		  plugins_enabled = false;
	bool		  plugin_found = false;
	bool		  show_commands = false;
	bool		  activation_test = false;
	pkg_init_flags	  init_flags = 0;
	struct plugcmd	 *c;
	const char	 *conffile = NULL;
	const char	 *reposdir = NULL;
	char		**save_argv;
	int		  j;

	struct option longopts[] = {
		{ "debug",		no_argument,		NULL,	'd' },
#ifdef HAVE_LIBJAIL
		{ "jail",		required_argument,	NULL,	'j' },
#endif
		{ "chroot",		required_argument,	NULL,	'c' },
		{ "config",		required_argument,	NULL,	'C' },
		{ "repo-conf-dir",	required_argument,	NULL,	'R' },
		{ "list",		no_argument,		NULL,	'l' },
		{ "version",		no_argument,		NULL,	'v' },
		{ "option",		required_argument,	NULL,	'o' },
		{ "only-ipv4",		no_argument,		NULL,	'4' },
		{ "only-ipv6",		no_argument,		NULL,	'6' },
		{ NULL,			0,			NULL,	0   },
	};

	/* Set stdout unbuffered */
	setvbuf(stdout, NULL, _IONBF, 0);

	/* Ignore SIGPIPE */
	signal(SIGPIPE, SIG_IGN);

	if (argc < 2)
		usage(NULL, NULL, stderr, PKG_USAGE_INVALID_ARGUMENTS, "not enough arguments");

	/* getopt_long() will permute the arg-list unless
	 * POSIXLY_CORRECT is set in the environment.  This is a
	 * difference to the original getopt() we were using, and
	 * screws up our 'pkg {pkg-opts} verb {verb-opts}' command
	 * line concept. */

	if (setenv("POSIXLY_CORRECT", "1",  1) == -1)
		err(EX_SOFTWARE, "setenv() failed");

	save_argv = argv;

#ifdef HAVE_LIBJAIL
	while ((ch = getopt_long(argc, argv, "+dj:c:C:R:lNvo:46", longopts, NULL)) != -1) {
#else
	while ((ch = getopt_long(argc, argv, "+dc:C:R:lNvo:46", longopts, NULL)) != -1) {
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
#ifdef HAVE_LIBJAIL
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
		case 'o':
			export_arg_option (optarg);
			break;
		case '4':
			init_flags = PKG_INIT_FLAG_USE_IPV4;
			break;
		case '6':
			init_flags = PKG_INIT_FLAG_USE_IPV6;
			break;
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;

	pkg_set_debug_level(debug);

	if (version == 1)
		show_version_info(version);

	if (show_commands && version == 0) {
		show_command_names();
		exit(EX_OK);
	}

	if (argc == 0 && version == 0 && !activation_test)
		usage(conffile, reposdir, stderr, PKG_USAGE_INVALID_ARGUMENTS, "no commands specified");

	umask(022);
	pkg_event_register(&event_callback, &debug);

	/* reset getopt for the next call */
	optreset = 1;
	optind = 1;

	if (debug == 0 && version == 0)
		start_process_worker(save_argv);

#ifdef HAVE_ARC4RANDOM
	/* Ensure that random is stirred after a possible fork */
	arc4random_stir();
#endif

	if (jail_str != NULL && chroot_path != NULL) {
		usage(conffile, reposdir, stderr, PKG_USAGE_INVALID_ARGUMENTS,
				"-j and -c cannot be used at the same time!\n");
	}

	if (chroot_path != NULL)
		if (chroot(chroot_path) == -1)
			errx(EX_SOFTWARE, "chroot failed!");

#ifdef HAVE_LIBJAIL
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

	if (pkg_ini(conffile, reposdir, init_flags) != EPKG_OK)
		errx(EX_SOFTWARE, "Cannot parse configuration file!");

	if (debug > 0)
		pkg_set_debug_level(debug);

	if (atexit(&pkg_shutdown) != 0)
		errx(EX_SOFTWARE, "register pkg_shutdown() to run at exit");

	if (!pkg_compiled_for_same_os_major())
		warnx("Warning: Major OS version upgrade detected.  Running "
		    "\"pkg-static install -f pkg\" recommended");


	plugins_enabled = pkg_object_bool(pkg_config_get("PKG_ENABLE_PLUGINS"));

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

	if (argc >= 1 && strcmp(argv[0], "bootstrap") == 0) {
		if (argc == 1) {
			printf("pkg(8) already installed, use -f to force.\n");
			exit(EXIT_SUCCESS);
		} else if (argc == 2 && strcmp(argv[1], "-f") == 0) {
			if (access("/usr/sbin/pkg", R_OK) == 0) {
				/* Only 10.0+ supported 'bootstrap -f' */
#if __FreeBSD_version < 1000502
				printf("Execute these steps to rebootstrap"
				     " pkg(8):\n");
				printf("# pkg delete -f pkg\n");
				printf("# /usr/sbin/pkg -v\n");
				exit(EXIT_SUCCESS);
#endif
				printf("pkg(8) is already installed. Forcing "
				    "reinstallation through pkg(7).\n");
				execl("/usr/sbin/pkg", "pkg", "bootstrap",
				    "-f", NULL);
				/* NOTREACHED */
			} else
				errx(EXIT_FAILURE, "pkg(7) bootstrapper not"
				    " found at /usr/sbin/pkg.");
		}
	}

	save_argv = argv;
	argc = expand_aliases(argc, &argv);

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

	set_globals();

	if (command == NULL) {
		/* Check if a plugin provides the requested command */
		ret = EPKG_FATAL;
		if (plugins_enabled) {
			STAILQ_FOREACH(c, &plugins, next) {
				if (strcmp(c->name, argv[0]) == 0) {
					plugin_found = true;
					ret = c->exec(argc, argv);
					break;
				}
			}
		}

		if (!plugin_found)
			usage(conffile, reposdir, stderr, PKG_USAGE_UNKNOWN_COMMAND, argv[0]);

		return (ret);
	}

	if (ambiguous <= 1) {
		assert(command->exec != NULL);
		ret = command->exec(argc, argv);
	} else {
		usage(conffile, reposdir, stderr, PKG_USAGE_UNKNOWN_COMMAND, argv[0]);
	}

	if (save_argv != argv)
		free(argv);

	if (ret == EX_OK && newpkgversion)
		return (EX_NEEDRESTART);

	return (ret);
}

