#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "info.h"

static struct commands {
	const char *name;
	const char *shortcut;
	void (*exec_cmd)(int argc, char **argv);
} cmd[] = { 
	{ "add", "a", NULL },
	{ "delete", "d", NULL},
	{ "info", "i", cmd_info},
	{ "update", "u", NULL},
	{ "help", "h", NULL},
	{ NULL, NULL, NULL },
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

	if (argc < 2)
		usage();

	for (i = 0; cmd[i].name != NULL; i++) {
		if (strncmp(argv[1], cmd[i].name, strlen(cmd[i].name)) == 0 ||
				strncmp(argv[1], cmd[i].shortcut, strlen(cmd[i].shortcut)) == 0) {
			argc--;
			argv++;
			if (cmd[i].exec_cmd != NULL)
				cmd[i].exec_cmd(argc, argv);
			else
				printf("%s: No yet implemented\n", cmd[i].name);
			break;
		}
	}

	return (EXIT_SUCCESS);
}
