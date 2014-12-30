/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
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

#include "pkg_config.h"

#include <sys/wait.h>
#ifdef HAVE_SYS_PROCCTL_H
#include <sys/procctl.h>
#endif

#include <assert.h>
#include <errno.h>
#include <paths.h>
#include <spawn.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

extern char **environ;

int
pkg_script_run(struct pkg * const pkg, pkg_script type)
{
	struct sbuf * const script_cmd = sbuf_new_auto();
	size_t i, j;
	int error, pstat;
	pid_t pid;
	const char *script_cmd_p;
	const char *argv[4];
	char **ep;
	int ret = EPKG_OK;
	int stdin_pipe[2] = {-1, -1};
	posix_spawn_file_actions_t action;
	bool use_pipe = 0;
	bool debug = false;
	ssize_t bytes_written;
	size_t script_cmd_len;
	long argmax;
#ifdef PROC_REAP_KILL
	struct procctl_reaper_status info;
	struct procctl_reaper_kill killemall;
#endif

	struct {
		const char * const arg;
		const pkg_script b;
		const pkg_script a;
	} const map[] = {
		/* a implies b with argument arg */
		{"PRE-INSTALL",    PKG_SCRIPT_INSTALL,   PKG_SCRIPT_PRE_INSTALL},
		{"POST-INSTALL",   PKG_SCRIPT_INSTALL,   PKG_SCRIPT_POST_INSTALL},
		{"PRE-UPGRADE",    PKG_SCRIPT_UPGRADE,   PKG_SCRIPT_PRE_UPGRADE},
		{"POST-UPGRADE",   PKG_SCRIPT_UPGRADE,   PKG_SCRIPT_POST_UPGRADE},
		{"DEINSTALL",      PKG_SCRIPT_DEINSTALL, PKG_SCRIPT_PRE_DEINSTALL},
		{"POST-DEINSTALL", PKG_SCRIPT_DEINSTALL, PKG_SCRIPT_POST_DEINSTALL},
	};

	if (!pkg_object_bool(pkg_config_get("RUN_SCRIPTS")))
		return (EPKG_OK);

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (map[i].a == type)
			break;
	}

	assert(i < sizeof(map) / sizeof(map[0]));

#ifdef PROC_REAP_KILL
	procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, NULL);
#endif
	for (j = 0; j < PKG_NUM_SCRIPTS; j++) {
		if (pkg_script_get(pkg, j) == NULL)
			continue;
		if (j == map[i].a || j == map[i].b) {
			sbuf_reset(script_cmd);
			setenv("PKG_PREFIX", pkg->prefix, 1);
			debug = pkg_object_bool(pkg_config_get("DEBUG_SCRIPTS"));
			if (debug)
				sbuf_printf(script_cmd, "set -x\n");
			pkg_sbuf_printf(script_cmd, "set -- %n-%v", pkg, pkg);

			if (j == map[i].b) {
				/* add arg **/
				sbuf_cat(script_cmd, " ");
				sbuf_cat(script_cmd, map[i].arg);
			}

			sbuf_cat(script_cmd, "\n");
			sbuf_cat(script_cmd, pkg_script_get(pkg, j));
			sbuf_finish(script_cmd);

			/* Determine the maximum argument length for the given
			   script to determine if /bin/sh -c can be used, or
			   if a pipe is required to /bin/sh -s. Similar to
			   find(1) determination */
			if ((argmax = sysconf(_SC_ARG_MAX)) == -1)
				argmax = _POSIX_ARG_MAX;
			argmax -= 1024;
			for (ep = environ; *ep != NULL; ep++)
				argmax -= strlen(*ep) + 1 + sizeof(*ep);
			argmax -= 1 + sizeof(*ep);

			pkg_debug(3, "Scripts: executing\n--- BEGIN ---\n%s\nScripts: --- END ---", sbuf_get(script_cmd));
			if (sbuf_len(script_cmd) > argmax) {
				if (pipe(stdin_pipe) < 0) {
					ret = EPKG_FATAL;
					goto cleanup;
				}

				posix_spawn_file_actions_init(&action);
				posix_spawn_file_actions_adddup2(&action, stdin_pipe[0],
				    STDIN_FILENO);
				posix_spawn_file_actions_addclose(&action, stdin_pipe[1]);

				argv[0] = _PATH_BSHELL;
				argv[1] = "-s";
				argv[2] = NULL;

				use_pipe = 1;
			} else {
				argv[0] = _PATH_BSHELL;
				argv[1] = "-c";
				argv[2] = sbuf_get(script_cmd);
				argv[3] = NULL;

				use_pipe = 0;
			}

			if ((error = posix_spawn(&pid, _PATH_BSHELL,
			    use_pipe ? &action : NULL,
			    NULL, __DECONST(char **, argv),
			    environ)) != 0) {
				errno = error;
				pkg_emit_errno("Cannot run script",
				    map[i].arg);
				goto cleanup;
			}

			if (use_pipe) {
				script_cmd_p = sbuf_get(script_cmd);
				script_cmd_len = sbuf_len(script_cmd);
				while (script_cmd_len > 0) {
					if ((bytes_written = write(stdin_pipe[1], script_cmd_p,
					    script_cmd_len)) == -1) {
						if (errno == EINTR)
							continue;
						ret = EPKG_FATAL;
						goto cleanup;
					}
					script_cmd_p += bytes_written;
					script_cmd_len -= bytes_written;
				}
				close(stdin_pipe[1]);
			}

			unsetenv("PKG_PREFIX");

			while (waitpid(pid, &pstat, 0) == -1) {
				if (errno != EINTR)
					goto cleanup;
			}

			if (WEXITSTATUS(pstat) != 0) {
				pkg_emit_error("%s script failed", map[i].arg);
				goto cleanup;
			}
		}
	}

cleanup:

	sbuf_delete(script_cmd);
	if (stdin_pipe[0] != -1)
		close(stdin_pipe[0]);
	if (stdin_pipe[1] != -1)
		close(stdin_pipe[1]);

#ifdef PROC_REAP_KILL
	procctl(P_PID, getpid(), PROC_REAP_STATUS, &info);
	if (info.rs_children != 0) {
		killemall.rk_sig = SIGKILL;
		killemall.rk_flags = 0;
		if (procctl(P_PID, getpid(), PROC_REAP_KILL, &killemall) != 0)
			pkg_emit_error("Fail to kill children of the scripts");
	}
	procctl(P_PID, getpid(), PROC_REAP_RELEASE, NULL);
#endif

	return (ret);
}

