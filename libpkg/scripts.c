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
#include <fcntl.h>
#include <paths.h>
#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <xstring.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

extern char **environ;

int
pkg_script_run(struct pkg * const pkg, pkg_script type, bool upgrade)
{
	xstring *script_cmd = NULL;
	size_t i, j, script_len;
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
	long argmax;
	int cur_pipe[2] = {-1, -1};
#ifdef PROC_REAP_KILL
	bool do_reap;
	pid_t mypid;
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
		{"DEINSTALL",      PKG_SCRIPT_DEINSTALL, PKG_SCRIPT_PRE_DEINSTALL},
		{"POST-DEINSTALL", PKG_SCRIPT_DEINSTALL, PKG_SCRIPT_POST_DEINSTALL},
	};

	if (!pkg_object_bool(pkg_config_get("RUN_SCRIPTS"))) {
		return (EPKG_OK);
	}

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (map[i].a == type)
			break;
	}

	assert(i < sizeof(map) / sizeof(map[0]));

#ifdef PROC_REAP_KILL
	mypid = getpid();
	do_reap = procctl(P_PID, mypid, PROC_REAP_ACQUIRE, NULL) == 0;
#endif
	for (j = 0; j < PKG_NUM_SCRIPTS; j++) {
		if (pkg_script_get(pkg, j) == NULL)
			continue;
		if (j == map[i].a || j == map[i].b) {
			xstring_renew(script_cmd);
			if (upgrade) {
				setenv("PKG_UPGRADE", "true", 1);
			}
			setenv("PKG_NAME", pkg->name, 1);
			setenv("PKG_PREFIX", pkg->prefix, 1);
			if (ctx.pkg_rootdir == NULL)
				ctx.pkg_rootdir = "/";
			setenv("PKG_ROOTDIR", ctx.pkg_rootdir, 1);
			if (ctx.ischrooted)
				setenv("PKG_CHROOTED", "true", 1);
			debug = pkg_object_bool(pkg_config_get("DEBUG_SCRIPTS"));
			if (debug)
				fprintf(script_cmd->fp, "set -x\n");
			pkg_fprintf(script_cmd->fp, "set -- %n-%v", pkg, pkg);

			if (j == map[i].b) {
				/* add arg **/
				fprintf(script_cmd->fp, " %s", map[i].arg);
			}

			fprintf(script_cmd->fp, "\n%s", pkg->scripts[j]->buf);

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

			fflush(script_cmd->fp);
			script_len = strlen(script_cmd->buf);
			pkg_debug(3, "Scripts: executing\n--- BEGIN ---\n%s\nScripts: --- END ---", script_cmd->buf);
			posix_spawn_file_actions_init(&action);
			if (get_socketpair(cur_pipe) == -1) {
				pkg_emit_errno("pkg_script_run", "socketpair");
				goto cleanup;
			}

			if (fcntl(cur_pipe[0], F_SETFL, O_NONBLOCK) == -1) {
				pkg_emit_errno("pkg_script_run", "fcntl");
				goto cleanup;
			}

			setenv("PKG_MSGFD", "4", 1);

			posix_spawn_file_actions_adddup2(&action, cur_pipe[1], 4);
			posix_spawn_file_actions_addclose(&action, cur_pipe[0]);
			/*
			 * consider cur_pipe[1] to probably be the lastest
			 * opened fd close all unuseful fd up to there
			 */
			for (int i = 5; i <= cur_pipe[1]; i++) {
				if (i != cur_pipe[0] && i != ctx.devnullfd)
					posix_spawn_file_actions_addclose(&action, i);
			}
			if (script_len > argmax) {
				if (pipe(stdin_pipe) < 0) {
					ret = EPKG_FATAL;
					posix_spawn_file_actions_destroy(&action);
					goto cleanup;
				}

				posix_spawn_file_actions_adddup2(&action, stdin_pipe[0],
				    STDIN_FILENO);
				posix_spawn_file_actions_addclose(&action, stdin_pipe[1]);

				argv[0] = _PATH_BSHELL;
				argv[1] = "-s";
				argv[2] = NULL;

				use_pipe = 1;
			} else {
				posix_spawn_file_actions_adddup2(&action,
				    ctx.devnullfd, STDIN_FILENO);

				argv[0] = _PATH_BSHELL;
				argv[1] = "-c";
				argv[2] = script_cmd->buf;
				argv[3] = NULL;

				use_pipe = 0;
			}

			if ((error = posix_spawn(&pid, _PATH_BSHELL, &action,
			    NULL, __DECONST(char **, argv),
			    environ)) != 0) {
				errno = error;
				pkg_errno("Cannot runscript %s", map[i].arg);
				posix_spawn_file_actions_destroy(&action);
				goto cleanup;
			}
			posix_spawn_file_actions_destroy(&action);

			if (use_pipe) {
				script_cmd_p = script_cmd->buf;
				while (script_len > 0) {
					if ((bytes_written = write(stdin_pipe[1], script_cmd_p,
					    script_len)) == -1) {
						if (errno == EINTR)
							continue;
						ret = EPKG_FATAL;
						goto cleanup;
					}
					script_cmd_p += bytes_written;
					script_len -= bytes_written;
				}
				close(stdin_pipe[1]);
			}

			unsetenv("PKG_PREFIX");

			close(cur_pipe[1]);
			cur_pipe[1] = -1;

			ret = pkg_script_run_child(pid, &pstat, cur_pipe[0], map[i].arg);

			close(cur_pipe[0]);
			cur_pipe[0] = -1;
		}
	}

cleanup:

	xstring_free(script_cmd);
	if (stdin_pipe[0] != -1)
		close(stdin_pipe[0]);
	if (stdin_pipe[1] != -1)
		close(stdin_pipe[1]);
	if (cur_pipe[0] != -1)
		close(cur_pipe[0]);
	if (cur_pipe[1] != -1)
		close(cur_pipe[1]);

#ifdef PROC_REAP_KILL
	/*
	 * If the prior PROCCTL_REAP_ACQUIRE call failed, the kernel
	 * probably doesn't support this, so don't try.
	 */
	if (!do_reap)
		return (ret);

	procctl(P_PID, mypid, PROC_REAP_STATUS, &info);
	if (info.rs_children != 0) {
		killemall.rk_sig = SIGKILL;
		killemall.rk_flags = 0;
		if (procctl(P_PID, mypid, PROC_REAP_KILL, &killemall) != 0) {
			pkg_errno("%s", "Fail to kill all processes");
		}
	}
	procctl(P_PID, mypid, PROC_REAP_RELEASE, NULL);
#endif

	return (ret);
}


int
pkg_script_run_child(int pid, int *pstat, int inputfd, const char* script_name) {
	struct pollfd pfd;
	bool wait_for_child;
	char msgbuf[16384+1];


	memset(&pfd, 0, sizeof(pfd));
	pfd.events = POLLIN | POLLERR | POLLHUP;
	pfd.fd = inputfd;

	// Wait for child to exit, and read input, including all queued input on child exit.
	wait_for_child = true;
	do {
		pfd.revents = 0;
		errno = 0;
		// Check if child is running, get exitstatus if newly terminated.
		pid_t p = 0;
		while (wait_for_child && (p = waitpid(pid, pstat, WNOHANG)) == -1) {
			if (errno != EINTR) {
				pkg_emit_error("waitpid() failed: %s",
				    strerror(errno));
				return (EPKG_FATAL);
			}
		}
		if (p > 0) {
			wait_for_child = false;
		}
		// Check for input from child, but only wait for more if child is still running.
		// Read/print all available input.
		ssize_t readsize;
		do {
			readsize = 0;
			int pres;
			while ((pres = poll(&pfd, 1, wait_for_child ? 1000 : 0)) == -1) {
				if (errno != EINTR) {
					pkg_emit_error("poll() failed: %s",
					    strerror(errno));
					return (EPKG_FATAL);
				}
			}
			if (pres > 0 && pfd.revents & POLLIN) {
				while ((readsize = read(inputfd, msgbuf, sizeof msgbuf - 1)) < 0) {
					// MacOS gives us ECONNRESET on child exit
					if (errno == EAGAIN || errno == ECONNRESET) {
						break;
					}
					if (errno != EINTR) {
						pkg_emit_errno(__func__, "read");
						return (EPKG_FATAL);
					}
				}
				if (readsize > 0) {
					msgbuf[readsize] = '\0';
					pkg_emit_message(msgbuf);
				}
			}
		} while (readsize > 0);
	} while (wait_for_child);

	if (WEXITSTATUS(*pstat) != 0) {
		if (WEXITSTATUS(*pstat) == 3)
			exit(0);

		pkg_emit_error("%s script failed", script_name);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}
