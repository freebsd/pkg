/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
#endif

#include <err.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <pwd.h>

#ifdef __linux__
# ifdef __GLIBC__
#  include <grp.h>
# endif
#endif

#include "pkg.h"

int
pkg_handle_sandboxed_call(pkg_sandbox_cb func, int fd, void *ud)
{
	pid_t pid;
	int status, ret;
	struct rlimit rl_zero;

	ret = -1;
	pid = fork();

	switch(pid) {
	case -1:
		warn("fork failed");
		return (EPKG_FATAL);
		break;
	case 0:
		break;
	default:
		/* Parent process */
		while (waitpid(pid, &status, 0) == -1) {
			if (errno != EINTR) {
				warn("Sandboxed process pid=%d", (int)pid);
				ret = -1;
				break;
			}
		}

		if (WIFEXITED(status)) {
			ret = WEXITSTATUS(status);
		}
		if (WIFSIGNALED(status)) {
			/* Process got some terminating signal, hence stop the loop */
			fprintf(stderr, "Sandboxed process pid=%d terminated abnormally by signal: %d\n",
					(int)pid, WTERMSIG(status));
			ret = -1;
		}
		return (ret);
	}

	rl_zero.rlim_cur = rl_zero.rlim_max = 0;
	if (setrlimit(RLIMIT_NPROC, &rl_zero) == -1)
		err(EXIT_FAILURE, "Unable to setrlimit(RLIMIT_NPROC)");

	/* Here comes child process */
#ifdef HAVE_CAPSICUM
#ifndef PKG_COVERAGE
	if (cap_enter() < 0 && errno != ENOSYS) {
		warn("cap_enter() failed");
		_exit(EXIT_FAILURE);
	}
#endif
#endif

	ret = func(fd, ud);

	_exit(ret);
}

int
pkg_handle_sandboxed_get_string(pkg_sandbox_cb func, char **result, int64_t *len,
		void *ud)
{
	pid_t pid;
	struct rlimit rl_zero;
	int	status, ret = EPKG_OK;
	int pair[2], r, allocated_len = 0, off = 0;
	char *buf = NULL;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1) {
		warn("socketpair failed");
		return (EPKG_FATAL);
	}

	pid = fork();

	switch(pid) {
	case -1:
		warn("fork failed");
		return (EPKG_FATAL);
		break;
	case 0:
		break;
	default:
		/* Parent process */
		close(pair[0]);
		/*
		 * We use blocking IO here as if the child is terminated we would have
		 * EINTR here
		 */
		buf = malloc(BUFSIZ);
		if (buf == NULL) {
			warn("malloc failed");
			return (EPKG_FATAL);
		}
		allocated_len = BUFSIZ;
		do {
			if (off >= allocated_len) {
				allocated_len *= 2;
				buf = realloc(buf, allocated_len);
				if (buf == NULL) {
					warn("realloc failed");
					return (EPKG_FATAL);
				}
			}

			r = read(pair[1], buf + off, allocated_len - off);
			if (r == -1 && errno != EINTR) {
				free(buf);
				warn("read failed");
				return (EPKG_FATAL);
			}
			else if (r > 0) {
				off += r;
			}
		} while (r > 0);

		/* Fill the result buffer */
		*len = off;
		*result = buf;
		if (*result == NULL) {
			warn("malloc failed");
			kill(pid, SIGTERM);
			ret = EPKG_FATAL;
		}
		while (waitpid(pid, &status, 0) == -1) {
			if (errno != EINTR) {
				warn("Sandboxed process pid=%d", (int)pid);
				ret = -1;
				break;
			}
		}

		if (WIFEXITED(status)) {
			ret = WEXITSTATUS(status);
		}
		if (WIFSIGNALED(status)) {
			/* Process got some terminating signal, hence stop the loop */
			fprintf(stderr, "Sandboxed process pid=%d terminated abnormally by signal: %d\n",
					(int)pid, WTERMSIG(status));
			ret = -1;
		}
		return (ret);
	}

	/* Here comes child process */
	close(pair[1]);

	pkg_drop_privileges();

	rl_zero.rlim_cur = rl_zero.rlim_max = 0;
	if (setrlimit(RLIMIT_NPROC, &rl_zero) == -1)
		err(EXIT_FAILURE, "Unable to setrlimit(RLIMIT_NPROC)");

#ifdef HAVE_CAPSICUM
#ifndef PKG_COVERAGE
	if (cap_enter() < 0 && errno != ENOSYS) {
		warn("cap_enter() failed");
		return (EPKG_FATAL);
	}
#endif
#endif

	ret = func(pair[0], ud);

	close(pair[0]);

	_exit(ret);
}

void
pkg_drop_privileges(void)
{
	struct passwd *nobody;

	if (geteuid() == 0) {
		nobody = getpwnam("nobody");
		if (nobody == NULL)
			errx(EXIT_FAILURE, "Unable to drop privileges: no 'nobody' user");
		setgroups(1, &nobody->pw_gid);
		/* setgid also sets egid and setuid also sets euid */
		if (setgid(nobody->pw_gid) == -1)
			err(EXIT_FAILURE, "Unable to setgid");
		if (setuid(nobody->pw_uid) == -1)
			err(EXIT_FAILURE, "Unable to setuid");
	}
}
