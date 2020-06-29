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
#include <utstring.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

extern char **environ;

int
pkg_script_run(struct pkg * const pkg, pkg_script type, bool upgrade)
{
	UT_string *script_cmd;
	size_t i, j;
	int error, pstat;
	pid_t pid;
	const char *script_cmd_p;
	const char *argv[4];
	char **ep;
	int ret = EPKG_OK;
	int fd = -1;
	int stdin_pipe[2] = {-1, -1};
	posix_spawn_file_actions_t action;
	bool use_pipe = 0;
	bool debug = false;
	ssize_t bytes_written;
	size_t script_cmd_len;
	long argmax;
	int cur_pipe[2];
#ifdef PROC_REAP_KILL
	bool do_reap;
	pid_t mypid;
	struct procctl_reaper_status info;
	struct procctl_reaper_kill killemall;
#endif
	struct pollfd pfd;
	bool should_waitpid;
	ssize_t linecap = 0;
	char *line = NULL;
	FILE *f;

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

	utstring_new(script_cmd);

	if (!pkg_object_bool(pkg_config_get("RUN_SCRIPTS"))) {
		utstring_free(script_cmd);
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
			utstring_clear(script_cmd);
			if (upgrade) {
				setenv("PKG_UPGRADE", "true", 1);
			}
			setenv("PKG_PREFIX", pkg->prefix, 1);
			if (ctx.pkg_rootdir == NULL)
				ctx.pkg_rootdir = "/";
			setenv("PKG_ROOTDIR", ctx.pkg_rootdir, 1);
			debug = pkg_object_bool(pkg_config_get("DEBUG_SCRIPTS"));
			if (debug)
				utstring_printf(script_cmd, "set -x\n");
			pkg_utstring_printf(script_cmd, "set -- %n-%v", pkg, pkg);

			if (j == map[i].b) {
				/* add arg **/
				utstring_printf(script_cmd, " %s", map[i].arg);
			}

			utstring_printf(script_cmd, "\n%s",
			    utstring_body(pkg->scripts[j]));

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

			pkg_debug(3, "Scripts: executing\n--- BEGIN ---\n%s\nScripts: --- END ---", utstring_body(script_cmd));
			posix_spawn_file_actions_init(&action);
			if (get_socketpair(cur_pipe) == -1) {
				pkg_emit_errno("pkg_run_script", "socketpair");
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
				if (i != cur_pipe[0])
					posix_spawn_file_actions_addclose(&action, i);
			}
			if (utstring_len(script_cmd) > argmax) {
				if (pipe(stdin_pipe) < 0) {
					ret = EPKG_FATAL;
					posix_spawn_file_actions_destroy(&action);
					close(cur_pipe[0]);
					close(cur_pipe[1]);
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
				fd = open("/dev/null", O_RDWR);
				if (fd < 0) {
					pkg_errno("Cannot open %s", "/dev/null");
					ret = EPKG_FATAL;
					posix_spawn_file_actions_destroy(&action);
					close(cur_pipe[0]);
					close(cur_pipe[1]);
					goto cleanup;
				}
				posix_spawn_file_actions_adddup2(&action,
				    fd, STDIN_FILENO);

				argv[0] = _PATH_BSHELL;
				argv[1] = "-c";
				argv[2] = utstring_body(script_cmd);
				argv[3] = NULL;

				use_pipe = 0;
			}

			if ((error = posix_spawn(&pid, _PATH_BSHELL, &action,
			    NULL, __DECONST(char **, argv),
			    environ)) != 0) {
				errno = error;
				pkg_errno("Cannot runscript %s", map[i].arg);
				posix_spawn_file_actions_destroy(&action);
				close(cur_pipe[0]);
				close(cur_pipe[1]);
				goto cleanup;
			}
			posix_spawn_file_actions_destroy(&action);

			if (fd != -1)
				close(fd);
			if (use_pipe) {
				script_cmd_p = utstring_body(script_cmd);
				script_cmd_len = utstring_len(script_cmd);
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

			close(cur_pipe[1]);
			memset(&pfd, 0, sizeof(pfd));
			pfd.fd = cur_pipe[0];
			pfd.events = POLLIN | POLLERR | POLLHUP;

			f = fdopen(pfd.fd, "r");
			should_waitpid = true;
			for (;;) {
				errno = 0;
				int pres = poll(&pfd, 1, 1000);
				if (pres == -1) {
					if (errno == EINTR) {
						continue;
					} else {
						pkg_emit_error("poll() "
						    "failed: %s", strerror(errno));
						ret = EPKG_FATAL;
						goto cleanup;
					}
				}
				if (pres == 0) {
					pid_t p;
					assert(should_waitpid);
					while ((p = waitpid(pid, &pstat, WNOHANG)) == -1) {
						if (errno != EINTR) {
							pkg_emit_error("waitpid() "
							    "failed: %s", strerror(errno));
							ret = EPKG_FATAL;
							goto cleanup;
						}
					}
					if (p > 0) {
						should_waitpid = false;
						break;
					}
					continue;
				}
				if (pfd.revents & (POLLERR|POLLHUP))
					break;
				if (getline(&line, &linecap, f) > 0)
					pkg_emit_message(line);
				if (feof(f))
					break;
			}
			fclose(f);

			while (should_waitpid && waitpid(pid, &pstat, 0) == -1) {
				if (errno != EINTR) {
					pkg_emit_error("waitpid() failed: %s",
					    strerror(errno));
					ret = EPKG_FATAL;
					goto cleanup;
				}
			}

			if (WEXITSTATUS(pstat) != 0) {
				if (WEXITSTATUS(pstat) == 3)
					exit(0);

				pkg_emit_error("%s script failed", map[i].arg);
				ret = EPKG_FATAL;
				goto cleanup;
			}
		}
	}

cleanup:

	free(line);
	utstring_free(script_cmd);
	if (stdin_pipe[0] != -1)
		close(stdin_pipe[0]);
	if (stdin_pipe[1] != -1)
		close(stdin_pipe[1]);

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

