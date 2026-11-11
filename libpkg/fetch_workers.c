/*
 * Copyright (c) 2026 FreeBSD Foundation
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <fetch.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "private/fetch.h"

/* Only values and strings cross the socket, never pointers to child memory. */
struct fetch_message {
	int type;
	int code;
	int fetch_error;
	uint32_t length[2];
	int64_t current;
	int64_t total;
};

#define FETCH_RESULT (-1)

struct fetch_worker {
	pid_t pid;
	int fd;
	size_t job;
	int64_t current;
	bool attempt_done;
};

struct fetch_pool {
	struct fetch_worker *workers;
	size_t count;
};

struct fetch_command {
	size_t job;
	uint32_t length;
};

struct fetch_worker_events {
	int fd;
	struct timespec last_tick;
	char error[512];
	int transport_error;
	bool transfer_started, transfer_finished, local_error;
};

static int
fetch_progress(pkg_fetch_progress_t status, struct pkg *pkg, int64_t current,
    size_t count)
{
	struct pkg_event ev = { .type = PKG_EVENT_FETCH_PROGRESS };

	ev.e_fetch_progress.status = status;
	ev.e_fetch_progress.pkg = pkg;
	ev.e_fetch_progress.current = current;
	ev.e_fetch_progress.total = pkg != NULL ? pkg->pkgsize : 0;
	ev.e_fetch_progress.packages = count;
	return (pkg_emit_event(&ev));
}

static bool
fetch_send(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	ssize_t n;

	while (len > 0) {
		n = send(fd, p, len, MSG_NOSIGNAL);
		if (n == -1 && errno == EINTR)
			continue;
		if (n <= 0)
			return (false);
		p += n;
		len -= n;
	}
	return (true);
}

static bool
fetch_recv(int fd, void *buf, size_t len)
{
	char *p = buf;
	ssize_t n;

	while (len > 0) {
		n = recv(fd, p, len, 0);
		if (n == -1 && errno == EINTR)
			continue;
		if (n <= 0)
			return (false);
		p += n;
		len -= n;
	}
	return (true);
}

static int
fetch_worker_event(void *data, struct pkg_event *ev)
{
	struct fetch_message msg = { .type = ev->type };
	struct fetch_worker_events *events = data;
	struct timespec now;
	const char *str[2] = { NULL, NULL };
	int fd = events->fd, reply;

	switch (ev->type) {
	case PKG_EVENT_ERROR:
	case PKG_EVENT_DEVELOPER_MODE:
		strlcpy(events->error, ev->e_pkg_error.msg, sizeof(events->error));
		return (0);
	case PKG_EVENT_NOTICE:
		str[0] = ev->e_pkg_notice.msg;
		break;
	case PKG_EVENT_DEBUG:
		str[0] = ev->e_debug.msg;
		msg.code = ev->e_debug.level;
		break;
	case PKG_EVENT_PKG_ERRNO:
		if (ev->e_errno.no == EPKG_NONETWORK)
			events->transport_error = FETCH_NETWORK;
		return (0);
	case PKG_EVENT_ERRNO:
		if (ev->e_errno.no == ETIMEDOUT)
			events->transport_error = FETCH_TIMEOUT;
		else if (ev->e_errno.no == ECONNRESET || ev->e_errno.no == ECONNREFUSED ||
		    ev->e_errno.no == ENETUNREACH || ev->e_errno.no == EHOSTUNREACH)
			events->transport_error = FETCH_NETWORK;
		else
			events->local_error = true;
		snprintf(events->error, sizeof(events->error), "%s(%s): %s",
		    ev->e_errno.func, ev->e_errno.arg ? ev->e_errno.arg : "",
		    strerror(ev->e_errno.no));
		return (0);
	case PKG_EVENT_FETCH_BEGIN:
	case PKG_EVENT_FETCH_FINISHED:
		events->transfer_started |= ev->type == PKG_EVENT_FETCH_BEGIN;
		events->transfer_finished |= ev->type == PKG_EVENT_FETCH_FINISHED;
		str[0] = ev->e_fetching.url;
		break;
	case PKG_EVENT_PKG_FETCH_BEGIN:
		/* The parent obtains the package from this worker's queue slot. */
		break;
	case PKG_EVENT_PROGRESS_START:
		/* The parent owns the package progress displays. */
		return (0);
	case PKG_EVENT_PROGRESS_TICK:
		/* Progress must not add a socket round trip for every data buffer. */
		if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
			if (ev->e_progress_tick.current < ev->e_progress_tick.total &&
			    now.tv_sec == events->last_tick.tv_sec &&
			    now.tv_nsec - events->last_tick.tv_nsec < 100000000)
				return (0);
			events->last_tick = now;
		}
		msg.current = ev->e_progress_tick.current;
		msg.total = ev->e_progress_tick.total;
		break;
	default:
		/* Fetch workers must not invoke interactive or database callbacks. */
		_exit(EXIT_FAILURE);
	}
	for (size_t i = 0; i < NELEM(str); i++) {
		if (str[i] != NULL)
			msg.length[i] = strlen(str[i]) + 1;
	}
	if (!fetch_send(fd, &msg, sizeof(msg)))
		_exit(EXIT_FAILURE);
	for (size_t i = 0; i < NELEM(str); i++) {
		if (!fetch_send(fd, str[i], msg.length[i]))
			_exit(EXIT_FAILURE);
	}
	if (!fetch_recv(fd, &reply, sizeof(reply)))
		_exit(EXIT_FAILURE);
	return (reply);
}

static void
fetch_disconnect(void)
{
	struct pkg_repo *repo = NULL;

	/* No worker may inherit a live connection owned by another process. */
	while (pkg_repos(&repo) == EPKG_OK) {
		if (repo->fetcher == NULL)
			continue;
		if (repo->fetcher->close != NULL)
			repo->fetcher->close(repo);
		if (repo->fetcher->cleanup != NULL)
			repo->fetcher->cleanup(repo);
		repo->fetcher = NULL;
	}
}

static int
fetch_package(struct pkg *pkg, bool mirror, const char *destdir, bool symlink)
{
	if (mirror)
		return (pkg_repo_mirror_package(pkg, destdir, symlink));
	return (pkg_repo_fetch_package(pkg));
}

static void
fetch_worker_main(int fd, struct pkg **pkgs, size_t count, bool mirror,
    const char *destdir, bool symlink)
{
	struct fetch_message msg = { .type = FETCH_RESULT };
	struct fetch_worker_events events = { .fd = fd };
	struct fetch_command command;
	struct fetch_response response;
	char target[PATH_MAX];

	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	pkg_event_register_fetch(fetch_worker_event, &events);
	while (fetch_recv(fd, &command, sizeof(command))) {
		if (command.job >= count || command.length == 0 || command.length > sizeof(target) ||
		    !fetch_recv(fd, target, command.length) || target[command.length - 1] != '\0')
			break;
		memset(&response, 0, sizeof(response));
		events.error[0] = '\0';
		events.transport_error = 0;
		events.transfer_started = events.transfer_finished = events.local_error = false;
		fetchLastErrCode = FETCH_OK;
		fetchResponse = &response;
		pkgs[command.job]->repo->fetch_target = target;
		msg.code = fetch_package(pkgs[command.job], mirror, destdir, symlink);
		msg.fetch_error = fetchLastErrCode;
		if (msg.fetch_error == FETCH_OK && msg.code != EPKG_OK) {
			if (events.transport_error)
				msg.fetch_error = events.transport_error;
			else if (events.transfer_started && !events.transfer_finished &&
			    !events.local_error && strncmp(target, "file:", 5) != 0)
				msg.fetch_error = FETCH_NETWORK;
		}
		if (msg.code != EPKG_OK && events.error[0] == '\0')
			strlcpy(events.error, fetchLastErrString, sizeof(events.error));
		pkgs[command.job]->repo->fetch_target = NULL;
		fetchResponse = NULL;
		/* Idle cached connections must not outlive the host reservation. */
		fetch_disconnect();
		msg.current = response.retry_after;
		msg.total = response.status;
		msg.length[0] = strlen(response.redirect) + 1;
		msg.length[1] = strlen(events.error) + 1;
		if (!fetch_send(fd, &msg, sizeof(msg)) ||
		    !fetch_send(fd, response.redirect, msg.length[0]) ||
		    !fetch_send(fd, events.error, msg.length[1]))
			break;
	}
	fetch_disconnect();
	close(fd);
	_exit(EXIT_SUCCESS);
}

static void
fetch_pool_stop(void *data)
{
	struct fetch_pool *pool = data;
	struct fetch_worker *w;
	int status;

	for (size_t i = 0; i < pool->count; i++) {
		w = &pool->workers[i];
		if (w->fd != -1) {
			close(w->fd);
			w->fd = -1;
		}
		if (w->pid > 0 && w->job != SIZE_MAX)
			kill(w->pid, SIGTERM);
	}
	for (size_t i = 0; i < pool->count; i++) {
		w = &pool->workers[i];
		if (w->pid > 0) {
			while (waitpid(w->pid, &status, 0) == -1 && errno == EINTR)
				;
			w->pid = 0;
		}
	}
}

static int
fetch_worker_read(struct fetch_worker *w, struct pkg *pkg, size_t count,
    struct pkg_fetch_plan *plan)
{
	struct fetch_message msg;
	struct pkg_event ev = { 0 };
	char *str[2] = { NULL, NULL };
	int reply = 0, ret = EPKG_OK;

	if (!fetch_recv(w->fd, &msg, sizeof(msg))) {
		pkg_emit_error("Download worker exited unexpectedly");
		return (EPKG_FATAL);
	}
	for (size_t i = 0; i < NELEM(str); i++) {
		if (msg.length[i] == 0)
			continue;
		if (msg.length[i] > 1024 * 1024) {
			ret = EPKG_FATAL;
			goto out;
		}
		str[i] = xmalloc(msg.length[i]);
		if (!fetch_recv(w->fd, str[i], msg.length[i]) ||
		    str[i][msg.length[i] - 1] != '\0') {
			ret = EPKG_FATAL;
			goto out;
		}
	}
	if (msg.type == FETCH_RESULT) {
		struct fetch_response response = { .status = msg.total, .retry_after = msg.current };
		strlcpy(response.redirect, str[0] ? str[0] : "", sizeof(response.redirect));
		w->attempt_done = true;
		ret = pkg_fetch_plan_result(plan, w->job, msg.code, msg.fetch_error, &response, str[1]);
		if (ret == EPKG_FATAL && str[1] && *str[1])
			pkg_emit_error("%s", str[1]);
		goto out;
	}
	ev.type = msg.type;
	switch (ev.type) {
	case PKG_EVENT_ERROR:
	case PKG_EVENT_DEVELOPER_MODE:
		ev.e_pkg_error.msg = str[0];
		break;
	case PKG_EVENT_NOTICE:
		ev.e_pkg_notice.msg = str[0];
		break;
	case PKG_EVENT_DEBUG:
		ev.e_debug.msg = str[0];
		ev.e_debug.level = msg.code;
		break;
	case PKG_EVENT_ERRNO:
	case PKG_EVENT_PKG_ERRNO:
		ev.e_errno.func = str[0];
		ev.e_errno.arg = str[1];
		ev.e_errno.no = msg.code;
		break;
	case PKG_EVENT_FETCH_BEGIN:
	case PKG_EVENT_FETCH_FINISHED:
		ev.e_fetching.url = str[0];
		ev.e_fetching.concurrent = true;
		break;
	case PKG_EVENT_PKG_FETCH_BEGIN:
		ev.e_pkg_fetching.pkg = pkg;
		break;
	case PKG_EVENT_PROGRESS_TICK:
		w->current = MAX(msg.current, 0);
		reply = fetch_progress(PKG_FETCH_PROGRESS_UPDATE, pkg, w->current, count);
		break;
	default:
		ret = EPKG_FATAL;
		goto out;
	}
	if (ev.type != PKG_EVENT_PROGRESS_TICK)
		reply = pkg_emit_event(&ev);
	if (!fetch_send(w->fd, &reply, sizeof(reply)))
		ret = EPKG_FATAL;
out:
	free(str[0]);
	free(str[1]);
	return (ret);
}

int
pkg_fetch_packages(struct pkg **pkgs, size_t count, bool mirror,
    const char *destdir, bool symlink)
{
	struct fetch_pool pool = { 0 };
	struct fetch_worker *w;
	struct pkg_fetch_plan *plan = NULL;
	const char *target;
	struct pollfd *fds;
	struct stat st;
	char **paths;
	char path[MAXPATHLEN];
	int (*pairs)[2];
	int ret = EPKG_OK, rc;
	int64_t limit, total = 0, done = 0, current, size;
	size_t finished = 0;

	limit = pkg_object_int(pkg_config_get("FETCH_WORKERS"));
	if (limit < 1) {
		pkg_emit_error("FETCH_WORKERS must be greater than zero");
		return (EPKG_FATAL);
	}
	if (count == 0)
		return (EPKG_OK);
	pool.count = MIN((uint64_t)limit, count);
	pool.workers = xcalloc(pool.count, sizeof(*pool.workers));
	fds = xcalloc(pool.count, sizeof(*fds));
	pairs = xcalloc(pool.count, sizeof(*pairs));
	paths = xcalloc(count, sizeof(*paths));
	for (size_t i = 0; i < pool.count; i++) {
		pool.workers[i].fd = -1;
		pool.workers[i].job = SIZE_MAX;
		pairs[i][0] = pairs[i][1] = -1;
	}
	for (size_t i = 0; i < count; i++) {
		path[0] = '\0';
		if (mirror)
			snprintf(path, sizeof(path), "%s/%s", destdir, pkgs[i]->repopath);
		else
			pkg_repo_cached_name(pkgs[i], path, sizeof(path));
		if (path[0] == '\0' || pkgs[i]->pkgsize < 0 ||
		    pkgs[i]->pkgsize > INT64_MAX - total) {
			pkg_emit_error("Invalid package download path or size");
			ret = EPKG_FATAL;
			goto out;
		}
		paths[i] = xstrdup(path);
		total += pkgs[i]->pkgsize;
	}
	plan = pkg_fetch_plan_new(pkgs, paths, count);
	if (plan == NULL) {
		ret = EPKG_FATAL;
		goto out;
	}
	fetch_disconnect();
	fflush(NULL);
	for (size_t i = 0; i < pool.count; i++) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, pairs[i]) == -1 ||
		    fcntl(pairs[i][0], F_SETFD, FD_CLOEXEC) == -1 ||
		    fcntl(pairs[i][1], F_SETFD, FD_CLOEXEC) == -1) {
			pkg_emit_errno("socketpair", "download worker");
			ret = EPKG_FATAL;
			goto out;
		}
	}
	pkg_register_cleanup_callback(fetch_pool_stop, &pool);
	for (size_t i = 0; i < pool.count; i++) {
		w = &pool.workers[i];
		w->pid = fork();
		if (w->pid == -1) {
			pkg_emit_errno("fork", "download worker");
			ret = EPKG_FATAL;
			goto stop;
		}
		if (w->pid == 0) {
			for (size_t k = 0; k < pool.count; k++) {
				close(pairs[k][0]);
				if (k != i)
					close(pairs[k][1]);
			}
			fetch_worker_main(pairs[i][1], pkgs, count, mirror, destdir, symlink);
		}
		w->fd = pairs[i][0];
		fds[i].fd = w->fd;
		fds[i].events = POLLIN;
	}
	for (size_t i = 0; i < pool.count; i++) {
		close(pairs[i][1]);
		pairs[i][0] = pairs[i][1] = -1;
	}
	fetch_progress(PKG_FETCH_PROGRESS_START, NULL, 0, count);
	/* Preserve aggregate progress for other libpkg consumers. */
	pkg_emit_progress_start("Downloading %zu packages", count);
	while (finished < count) {
		for (size_t i = 0; i < pool.count; i++) {
			w = &pool.workers[i];
			if (w->job != SIZE_MAX)
				continue;
			size_t job;
			rc = pkg_fetch_plan_next(plan, &job, &target);
			if (rc < 0) {
				ret = EPKG_FATAL;
				goto stop;
			}
			if (rc == 0)
				break;
			w->job = job;
			w->attempt_done = false;
			w->current = 0;
			if (stat(paths[job], &st) == 0 && st.st_size > 0)
				w->current = MIN(st.st_size, pkgs[job]->pkgsize);
			if (fetch_progress(PKG_FETCH_PROGRESS_UPDATE, pkgs[job], w->current, count)) {
				ret = EPKG_CANCEL;
				goto stop;
			}
			struct fetch_command command = {
				.job = job, .length = strlen(target) + 1,
			};
			if (command.length > PATH_MAX || !fetch_send(w->fd, &command, sizeof(command)) ||
			    !fetch_send(w->fd, target, command.length)) {
				ret = EPKG_FATAL;
				goto stop;
			}
		}

		rc = poll(fds, pool.count, 100);
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			pkg_emit_errno("poll", "download workers");
			ret = EPKG_FATAL;
			goto stop;
		}
		for (size_t i = 0; i < pool.count; i++) {
			w = &pool.workers[i];
			if (fds[i].revents == 0)
				continue;
			rc = fetch_worker_read(w,
			    w->job != SIZE_MAX ? pkgs[w->job] : NULL, count, plan);
			if (rc == EPKG_END) {
				fetch_progress(PKG_FETCH_PROGRESS_FINISHED,
				    pkgs[w->job], pkgs[w->job]->pkgsize, count);
				done += pkgs[w->job]->pkgsize;
				w->job = SIZE_MAX;
				finished++;
			} else if (rc == EPKG_OK && w->attempt_done) {
				fetch_progress(PKG_FETCH_PROGRESS_PENDING, pkgs[w->job], 0, count);
				w->job = SIZE_MAX;
			} else if (rc != EPKG_OK) {
				ret = rc;
				goto stop;
			}
		}
		current = done;
		for (size_t i = 0; i < pool.count; i++) {
			w = &pool.workers[i];
			if (w->job == SIZE_MAX)
				continue;
			size = pkgs[w->job]->pkgsize;
			/* Reach 100% only after the checksum has been verified. */
			current += MIN(w->current, MAX(size - 1, 0));
		}
		if (pkg_emit_progress_tick(current, total)) {
			ret = EPKG_CANCEL;
			goto stop;
		}
	}
stop:
	fetch_progress(PKG_FETCH_PROGRESS_END, NULL, 0, count);
	for (size_t i = 0; i < pool.count; i++) {
		if (pool.workers[i].fd != -1)
			pairs[i][0] = -1;
		if (pairs[i][1] != -1) {
			close(pairs[i][1]);
			pairs[i][1] = -1;
		}
	}
	fetch_pool_stop(&pool);
	pkg_unregister_cleanup_callback(fetch_pool_stop, &pool);
out:
	for (size_t i = 0; i < pool.count; i++) {
		if (pairs[i][0] != -1)
			close(pairs[i][0]);
		if (pairs[i][1] != -1)
			close(pairs[i][1]);
	}
	for (size_t i = 0; i < count; i++)
		free(paths[i]);
	free(paths);
	pkg_fetch_plan_free(plan);
	free(pairs);
	free(fds);
	free(pool.workers);
	return (ret);
}
