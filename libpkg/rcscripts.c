/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

static int rc_stop(const char *);
static int rc_start(const char *);

extern char **environ;

int
pkg_start_stop_rc_scripts(struct pkg *pkg, pkg_rc_attr attr)
{
	struct pkg_file *file = NULL;
	char rc_d_path[PATH_MAX];
	const char *rcfile;
	const char *rc;
	size_t len = 0;
	int ret = 0;

	snprintf(rc_d_path, sizeof(rc_d_path), "%s/etc/rc.d/", pkg->prefix);
	len = strlen(rc_d_path);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (strncmp(rc_d_path, file->path, len) == 0) {
			rcfile = file->path;
			rcfile += len;
			rc = strrchr(rcfile, '/');
			rc++;
			switch (attr) {
			case PKG_RC_START:
				ret += rc_start(rcfile);
				break;
			case PKG_RC_STOP:
				ret += rc_stop(rcfile);
				break;
			}
		}
	}

	return (ret);
}

static int
rc_stop(const char *rc_file)
{
	int error, pstat;
	pid_t pid;
	posix_spawn_file_actions_t actions;
	const char *argv[4];

	if (rc_file == NULL)
		return (0);

	argv[0] = "service";
	argv[1] = rc_file;
	argv[2] = "onestatus";
	argv[3] = NULL;

	if ((error = posix_spawn_file_actions_init(&actions)) != 0 ||
	    (error = posix_spawn_file_actions_addopen(&actions,
	    STDOUT_FILENO, "/dev/null", O_RDONLY, 0)) != 0 ||
	    (error = posix_spawn_file_actions_addopen(&actions,
	    STDERR_FILENO, "/dev/null", O_RDONLY, 0)) != 0 ||
	    (error = posix_spawn(&pid, "/usr/sbin/service", &actions, NULL,
	    __DECONST(char **, argv), environ)) != 0) {
		errno = error;
		pkg_emit_errno("Cannot query service", rc_file);
		return (-1);
	}

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (-1);
	}

	if (WEXITSTATUS(pstat) != 0)
		return (0);

	posix_spawn_file_actions_destroy(&actions);

	argv[2] = "stop";

	if ((error = posix_spawn(&pid, "/usr/sbin/service", NULL, NULL,
	    __DECONST(char **, argv), environ)) != 0) {
		errno = error;
		pkg_emit_errno("Cannot stop service", rc_file);
		return (-1);
	}

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (-1);
	}

	return (WEXITSTATUS(pstat));
}

static int
rc_start(const char *rc_file)
{
	int error, pstat;
	pid_t pid;
	const char *argv[4];

	if (rc_file == NULL)
		return (0);

	argv[0] = "service";
	argv[1] = rc_file;
	argv[2] = "quietstart";
	argv[3] = NULL;

	if ((error = posix_spawn(&pid, "/usr/sbin/service", NULL, NULL,
	    __DECONST(char **, argv), environ)) != 0) {
		errno = error;
		pkg_emit_errno("Cannot start service", rc_file);
		return (-1);
	}

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (-1);
	}

	return (WEXITSTATUS(pstat));
}


