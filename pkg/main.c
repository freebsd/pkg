#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <err.h>

#include "info.h"

static struct commands {
	const char *name;
	void (*exec_cmd)(int argc, char **argv);
} cmd[] = { 
	{ "add", NULL },
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
	int ambiguous = 0;
	size_t len;

	if (argc < 2)
		usage();

	len = strlen(argv[1]);
	for (i = 0; cmd[i].name != NULL; i++) {
		if (strncmp(argv[1], cmd[i].name, len) == 0) {
			/* if we have the exact cmd */
			if (len == sizeof(cmd[i].name) - 1) {
				command = &cmd[i];
				ambiguous = 0;
				break;
			}

			/*
			 * we already found a partial match so `argv[1]' is
			 * an ambigous shortcut
			 */
			if (command != NULL)
				ambiguous = 1;

			command = &cmd[i];
		}
	}

	if (command == NULL)
		usage();

	if (ambiguous == 0) {
		argc--;
		argv++;
		if (command->exec_cmd != NULL) 
			command->exec_cmd(argc, argv);
		else
			printf("%s: No yet implemented\n", command->name);
	}

	if (ambiguous == 1) {
		warnx("Ambiguous command: %s, could be:", argv[1]);
		for (i = 0; cmd[i].name != NULL; i++) {
			if (strlen(cmd[i].name) < strlen(argv[1]))
				continue;
			if (strncmp(argv[1], cmd[i].name, strlen(argv[1])) == 0)
				warnx("\t%s",cmd[i].name);
		}
	}

	return (EXIT_SUCCESS);
}
