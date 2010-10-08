#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <err.h>

#include "create.h"
#include "info.h"

static void usage(void);

static struct commands {
	const char *name;
	int (*exec_cmd)(int argc, char **argv);
} cmd[] = { 
	{ "add", NULL },
	{ "create", cmd_create},
	{ "delete", NULL},
	{ "info", cmd_info},
	{ "install", NULL},
	{ "update", NULL},
	{ "help", NULL},
	{ NULL, NULL },
};

static void
usage()
{
	fprintf(stderr, "usage: ...");
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	int i;
	struct commands *command = NULL;
	int ambiguous = -1;
	size_t len;

	if (argc < 2)
		usage();

	len = strlen(argv[1]);
	for (i = 0; cmd[i].name != NULL; i++) {
		if (strncmp(argv[1], cmd[i].name, len) == 0) {
			/* if we have the exact cmd */
			if (len == strlen(cmd[i].name)) {
				command = &cmd[i];
				ambiguous = 0;
				break;
			}

			/*
			 * we already found a partial match so `argv[1]' is
			 * an ambiguous shortcut
			 */
			if (command != NULL)
				ambiguous = 1;
			else
				ambiguous = 0;

			command = &cmd[i];
		}
	}

	if (command == NULL)
		usage();

	if (ambiguous == 0) {
		argc--;
		argv++;
		if (command->exec_cmd != NULL)
			return (command->exec_cmd(argc, argv));
		else
			printf("%s: No yet implemented\n", command->name);
	}

	if (ambiguous == 1) {
		warnx("Ambiguous command: %s, could be:", argv[1]);
		for (i = 0; cmd[i].name != NULL; i++)
			if (strncmp(argv[1], cmd[i].name, len) == 0)
				warnx("\t%s",cmd[i].name);
	}

	return (EX_USAGE);
}
