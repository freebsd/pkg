#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/jail.h>
#include <jail.h>

#include <pkg.h>

#include "add.h"
#include "autoremove.h"
#include "backup.h"
#include "create.h"
#include "delete.h"
#include "event.h"
#include "info.h"
#include "install.h"
#include "register.h"
#include "repo.h"
#include "search.h"
#include "update.h"
#include "upgrade.h"
#include "version.h"
#include "which.h"

static void usage(void);
static void usage_help(void);
static int exec_help(int, char **);

static struct commands {
	const char * const name;
	int (*exec)(int argc, char **argv);
	void (* const usage)(void);
} cmd[] = {
	{ "add", exec_add, usage_add},
	{ "autoremove", exec_autoremove, usage_autoremove},
	{ "backup", exec_backup, usage_backup},
	{ "create", exec_create, usage_create},
	{ "delete", exec_delete, usage_delete},
	{ "help", exec_help, usage_help},
	{ "info", exec_info, usage_info},
	{ "install", exec_install, usage_install},
	{ "search", exec_search, usage_search},
	{ "register", exec_register, usage_register},
	{ "repo", exec_repo, usage_repo},
	{ "update", exec_update, usage_update},
	{ "upgrade", exec_upgrade, usage_upgrade},
	{ "version", exec_version, usage_version},
	{ "which", exec_which, usage_which},
};

const unsigned int cmd_len = (sizeof(cmd)/sizeof(cmd[0]));

static void
usage(void)
{
	fprintf(stderr, "usage: pkg [-d] [-j <jail name or id>|-c <chroot path>] <command> [<args>]\n\n");
	fprintf(stderr, "Global options supported:\n");
	fprintf(stderr, "  -d: Increment debug level\n\n");
	fprintf(stderr, "Commands supported:\n");

	for (unsigned int i = 0; i < cmd_len; i++) 
		fprintf(stderr, "\t%s\n", cmd[i].name);

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
	char ch;
	int debug = 0;

	if (argc < 2)
		usage();

	pkg_event_register(&event_callback, NULL);

	while ((ch = getopt(argc, argv, "dj:c:")) != -1) {
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
			default:
				break;
		}
	}
	argc -= optind;
	argv += optind;
	/* reset getopt for the next call */
	optreset = 1;
	optind = 1;

	if (jail_str != NULL && chroot_path != NULL) {
		fprintf(stderr, "-j and -c can be used at the same time\n");
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
		usage();
		return (EX_USAGE); /* Not reached but makes scanbuild happy */
	}

	if (ambiguous <= 1) {
		assert(command->exec != NULL);
		return (command->exec(argc, argv));
	} else {
		warnx("'%s' is not a valid command.\n", argv[0]);

		fprintf(stderr, "See 'pkg help' for more information on the commands.\n\n");
		fprintf(stderr, "Command '%s' could be one of the following:\n", argv[0]);

		for (i = 0; i < cmd_len; i++)
			if (strncmp(argv[0], cmd[i].name, len) == 0)
				fprintf(stderr, "\t%s\n",cmd[i].name);
	}

	return (EX_USAGE);
}

