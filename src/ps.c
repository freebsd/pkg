/*-
 * Copyright (c) 2024 Ricardo Branco <rbranco@suse.de>
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <libprocstat.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <vis.h>

#include <pkg.h>

#include "pkgcli.h"

static char *safe_string(char *);
static void print_argv(struct procstat *, struct kinfo_proc *);
static void print_proc(struct procstat *, struct kinfo_proc *, int);

void
usage_ps(void)
{
	fprintf(stderr,
	    "Usage: pkg ps [-v]\n\n");
	fprintf(stderr, "For more information see 'pkg help install'.\n");
}

int
exec_ps(int argc, char **argv)
{
	struct kinfo_proc	*procs;
	struct procstat		*ps;
	int		 	 ch;
	unsigned int		 count;
	bool		 	 verbose = false;

	struct option longopts[] = {
		{ "verbose",		no_argument,		NULL,	'v' },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "v", longopts, NULL)) != -1) {
		switch (ch) {
		case 'v':
			verbose = true;
			break;
		default:
			usage_install();
			return (EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0) {
		usage_ps();
		return (EXIT_FAILURE);
	}

	/* Will fail if security.bsd.unprivileged_proc_debug=0 */
	ps = procstat_open_sysctl();
	if (ps == NULL)
		err(1, "procstat_open_sysctl");

	procs = procstat_getprocs(ps, KERN_PROC_PROC, 0, &count);
	if (procs == NULL) {
		procstat_close(ps);
		err(1, "procstat_getprocs");
	}

	printf("PID\tPPID\tUID\tUser\tCommand\n");
	for (unsigned int i = 0; i < count; i++)
		if (procs[i].ki_pid != 0)
			print_proc(ps, &procs[i], verbose);

	procstat_freeprocs(ps, procs);
	procstat_close(ps);
	return (0);
}

static void
print_proc(struct procstat *ps, struct kinfo_proc *kp, int verbose)
{
	unsigned int	count;

	struct kinfo_vmentry *vmmap = procstat_getvmmap(ps, kp, &count);
	if (vmmap == NULL) {
		if (errno != EPERM && errno != ENOENT)
			err(1, "procstat_getvmmap: %d", kp->ki_pid);
		return;
	}

	for (unsigned int i = 0; i < count; i++)
		/* Print executable mappings with no path */
		if (vmmap[i].kve_type == KVME_TYPE_VNODE &&
		    vmmap[i].kve_protection & KVME_PROT_EXEC &&
		    vmmap[i].kve_path[0] == '\0') {
			printf("%d\t%d\t%d\t%s\t%s\n", kp->ki_pid, kp->ki_ppid,
				kp->ki_ruid, kp->ki_login, kp->ki_comm);
			if (verbose)
				print_argv(ps, kp);
			break;
		}

	procstat_freevmmap(ps, vmmap);
}

static void
print_argv(struct procstat *ps, struct kinfo_proc *kp)
{
	char **argv = procstat_getargv(ps, kp, 0);
	if (argv == NULL) {
		warn("procstat_getargv: %d", kp->ki_pid);
		return;
	}

	printf("\t");
	do {
		printf(" %s", safe_string(*argv));
	} while (*++argv);
	printf("\n");

	procstat_freeargv(ps);
}

static char *
safe_string(char *arg) {
	static char *vis = NULL;

	if (vis == NULL) {
		vis = malloc(PATH_MAX * 4 + 1);
		if (vis == NULL)
			err(1, "malloc");
	}
	(void)strvis(vis, arg, VIS_TAB | VIS_NL | VIS_CSTYLE);

	return vis;
}
