/*-
 * Copyright (c) 2011-2026 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "xmalloc.h"
#include "pkghash.h"

extern char **environ;

/*
 * Run a command and return its exit status, or -1 on error.
 * If quiet is true, stdout and stderr are redirected to /dev/null.
 */
static int
run_cmd(const char *program, const char **argv, bool quiet)
{
	posix_spawn_file_actions_t actions, *actionsp = NULL;
	int error, pstat;
	pid_t pid;

	if (quiet) {
		if ((error = posix_spawn_file_actions_init(&actions)) != 0 ||
		    (error = posix_spawn_file_actions_addopen(&actions,
		    STDOUT_FILENO, "/dev/null", O_RDONLY, 0)) != 0 ||
		    (error = posix_spawn_file_actions_addopen(&actions,
		    STDERR_FILENO, "/dev/null", O_RDONLY, 0)) != 0) {
			posix_spawn_file_actions_destroy(&actions);
			errno = error;
			return (-1);
		}
		actionsp = &actions;
	}

	error = posix_spawn(&pid, program, actionsp, NULL,
	    __DECONST(char **, argv), environ);
	if (actionsp != NULL)
		posix_spawn_file_actions_destroy(actionsp);
	if (error != 0) {
		errno = error;
		return (-1);
	}

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (-1);
	}

	return (WEXITSTATUS(pstat));
}

/*
 * Run "service <name> <cmd>" quietly and return exit status.
 */
static int
service_cmd(const char *name, const char *cmd)
{
	const char *argv[4];

	argv[0] = "service";
	argv[1] = name;
	argv[2] = cmd;
	argv[3] = NULL;
	return (run_cmd("/usr/sbin/service", argv, true));
}

/*
 * Run "<script_path> <cmd>" quietly and return exit status.
 */
static int
script_cmd(const char *script_path, const char *cmd)
{
	const char *argv[3];

	argv[0] = script_path;
	argv[1] = cmd;
	argv[2] = NULL;
	return (run_cmd(script_path, argv, true));
}

static int
rc_stop(const char *rc_file)
{
	if (rc_file == NULL)
		return (0);

	/* use faststrop to avoid checking if the servie was running */
	return (service_cmd(rc_file, "faststop"));
}

static int
rc_stop_with_script(const char *script_path)
{
	if (script_path == NULL)
		return (0);

	/* use faststrop to avoid checking if the servie was running */
	return (script_cmd(script_path, "faststop"));
}

static int
rc_start(const char *rc_file)
{
	if (rc_file == NULL)
		return (0);

	return (service_cmd(rc_file, "start"));
}

int
pkg_start_stop_rc_scripts(struct pkg *pkg, pkg_rc_attr attr)
{
	struct pkg_file *file = NULL;
	char rc_d_path[PATH_MAX];
	const char *rcfile;
	size_t len = 0;
	int ret = 0;
	bool handle_rc;

	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));
	if (!handle_rc)
		return (ret);

	/* Do not manage rc scripts when operating on an alternate rootdir */
	if (ctx.pkg_rootdir != NULL)
		return (ret);

	snprintf(rc_d_path, sizeof(rc_d_path), "%s/etc/rc.d/", pkg->prefix);
	len = strlen(rc_d_path);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (strncmp(rc_d_path, file->path, len) == 0) {
			rcfile = file->path;
			rcfile += len;
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
copy_file(const char *src, const char *dst)
{
	int sfd, dfd;
	char buf[BUFSIZ];
	ssize_t n;
	struct stat sb;

	sfd = open(src, O_RDONLY);
	if (sfd == -1)
		return (-1);

	dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0700);
	if (dfd == -1) {
		close(sfd);
		return (-1);
	}

	for (;;) {
		n = read(sfd, buf, sizeof(buf));
		if (n == -1) {
			if (errno == EINTR)
				continue;
			close(sfd);
			close(dfd);
			return (-1);
		}
		if (n == 0)
			break;
		const char *p = buf;
		ssize_t remaining = n;
		while (remaining > 0) {
			ssize_t w = write(dfd, p, remaining);
			if (w == -1) {
				if (errno == EINTR)
					continue;
				close(sfd);
				close(dfd);
				return (-1);
			}
			p += w;
			remaining -= w;
		}
	}

	if (fstat(sfd, &sb) == 0)
		fchmod(dfd, sb.st_mode);

	close(sfd);
	close(dfd);
	return (n < 0 ? -1 : 0);
}

void
pkg_deferred_rc_init(struct deferred_rc *rc)
{
	memset(rc, 0, sizeof(*rc));
}

static void
deferred_rc_cleanup_cb(void *data)
{
	struct deferred_rc *rc = data;

	pkg_deferred_rc_free(rc);
}

static void
deferred_rc_stop_free(struct deferred_rc_stop *s)
{
	if (s->oldpath != NULL) {
		unlink(s->oldpath);
		free(s->oldpath);
	}
	free(s->name);
}

void
pkg_deferred_rc_free(struct deferred_rc *rc)
{
	if (rc == NULL)
		return;

	vec_foreach(rc->to_stop, i)
		deferred_rc_stop_free(&rc->to_stop.d[i]);
	vec_free(&rc->to_stop);

	vec_foreach(rc->to_start, i)
		free(rc->to_start.d[i]);
	vec_free(&rc->to_start);

	pkghash_destroy(rc->seen_stop);
	rc->seen_stop = NULL;
	pkghash_destroy(rc->seen_start);
	rc->seen_start = NULL;

	if (rc->tmpdir != NULL) {
		rmdir(rc->tmpdir);
		free(rc->tmpdir);
		rc->tmpdir = NULL;
	}
}

static int
deferred_rc_ensure_tmpdir(struct deferred_rc *rc)
{
	if (rc->tmpdir != NULL)
		return (0);

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";

	char template[PATH_MAX];
	snprintf(template, sizeof(template), "%s/pkg-rc.XXXXXX", tmpdir);
	if (mkdtemp(template) == NULL) {
		pkg_errno("Cannot create temporary directory '%s'", template);
		return (-1);
	}
	rc->tmpdir = xstrdup(template);

	pkg_register_cleanup_callback(deferred_rc_cleanup_cb, rc);

	return (0);
}

void
pkg_deferred_rc_add(struct deferred_rc *rc, struct pkg *pkg, pkg_rc_attr attr)
{
	struct pkg_file *file = NULL;
	char rc_d_path[PATH_MAX];
	size_t len;
	bool handle_rc;

	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));
	if (!handle_rc)
		return;

	/* Do not manage rc scripts when operating on an alternate rootdir */
	if (ctx.pkg_rootdir != NULL)
		return;

	snprintf(rc_d_path, sizeof(rc_d_path), "%s/etc/rc.d/", pkg->prefix);
	len = strlen(rc_d_path);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (strncmp(rc_d_path, file->path, len) != 0)
			continue;

		const char *rcname = file->path + len;

		switch (attr) {
		case PKG_RC_STOP:
			if (pkghash_get(rc->seen_stop, rcname) != NULL)
				break;
			pkghash_safe_add(rc->seen_stop, rcname, NULL, NULL);

			struct deferred_rc_stop entry = { .name = xstrdup(rcname) };
			if (deferred_rc_ensure_tmpdir(rc) == 0) {
				char saved[PATH_MAX];
				snprintf(saved, sizeof(saved), "%s/%s",
				    rc->tmpdir, rcname);
				if (copy_file(file->path, saved) == 0)
					entry.oldpath = xstrdup(saved);
				else
					pkg_debug(1,
					    "Failed to save rc script %s, "
					    "will use service(8) to stop",
					    file->path);
			}
			vec_push(&rc->to_stop, entry);
			break;
		case PKG_RC_START:
			if (pkghash_get(rc->seen_start, rcname) != NULL)
				break;
			pkghash_safe_add(rc->seen_start, rcname, NULL, NULL);
			vec_push(&rc->to_start, xstrdup(rcname));
			break;
		}
	}
}

int
pkg_deferred_rc_execute(struct deferred_rc *rc)
{
	int ret = 0;

	/*
	 * Upgrades (in both stop and start sets): "service <name> restart"
	 * so the rc script can handle the transition gracefully.
	 * Deletions (stop only): stop using the saved old script.
	 * New installs (start only): start if enabled.
	 */
	vec_foreach(rc->to_stop, i) {
		struct deferred_rc_stop *s = &rc->to_stop.d[i];
		if (pkghash_get(rc->seen_start, s->name) != NULL) {
			ret += service_cmd(s->name, "restart");
		} else {
			if (s->oldpath != NULL) {
				ret += rc_stop_with_script(s->oldpath);
			} else {
				ret += rc_stop(s->name);
			}
		}
	}

	/* Start only services that were not already restarted above */
	vec_foreach(rc->to_start, i) {
		char *name = rc->to_start.d[i];
		if (pkghash_get(rc->seen_stop, name) != NULL)
			continue;
		pkg_emit_notice("Starting %s", name);
		ret += rc_start(name);
	}

	if (rc->tmpdir != NULL)
		pkg_unregister_cleanup_callback(deferred_rc_cleanup_cb, rc);
	pkg_deferred_rc_free(rc);

	return (ret);
}
