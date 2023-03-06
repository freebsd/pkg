/*-
 * Copyright (c) 2020 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fetch.h>
#include <paths.h>
#include <poll.h>
#include <netdb.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/fetch.h"
#include "private/utils.h"

static int ssh_read(void *data, char *buf, int len);
static int ssh_write(void *data, const char *buf, int l);
static int ssh_close(void *data);
static int tcp_close(void *data);

static int
tcp_connect(struct pkg_repo *repo, struct url *u)
{
	char *line = NULL;
	size_t linecap = 0;
	struct addrinfo *ai = NULL, *curai, hints;
	char srv[NI_MAXSERV];
	int sd = -1;
	int retcode;

	pkg_debug(1, "TCP> tcp_connect");
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	if ((repo->flags & REPO_FLAGS_USE_IPV4) == REPO_FLAGS_USE_IPV4)
		hints.ai_family = PF_INET;
	else if ((repo->flags & REPO_FLAGS_USE_IPV6) == REPO_FLAGS_USE_IPV6)
		hints.ai_family = PF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(srv, sizeof(srv), "%d", u->port);
	if (getaddrinfo(u->host, srv, &hints, &ai) != 0) {
		pkg_emit_error("Unable to lookup for '%s'", u->host);
		return (EPKG_FATAL);
	}
	for (curai = ai; curai != NULL; curai = curai->ai_next) {
		if ((sd = socket(curai->ai_family, curai->ai_socktype,
		    curai->ai_protocol)) == -1)
			continue;
		if (connect(sd, curai->ai_addr, curai->ai_addrlen) == -1) {
			close(sd);
			sd = -1;
			continue;
		}
		break;
	}
	freeaddrinfo(ai);
	if (sd == -1) {
		pkg_emit_error("Could not connect to tcp://%s:%d", u->host,
		    u->port);
		return (EPKG_FATAL);
	}
	if (setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, &(int){ 1 }, sizeof(int)) != 0) {
		pkg_emit_errno("Could not connect", "setsockopt");
		close(sd);
		return (EPKG_FATAL);
	}
	repo->sshio.in = dup(sd);
	repo->sshio.out = dup(sd);
	repo->ssh = funopen(repo, ssh_read, ssh_write, NULL, tcp_close);

	retcode = EPKG_FATAL;
	if (repo->ssh == NULL) {
		pkg_emit_errno("Failed to open stream", "tcp_connect");
		goto tcp_cleanup;
	}

	if (getline(&line, &linecap, repo->ssh) > 0) {
		if (strncmp(line, "ok:", 3) != 0) {
			pkg_debug(1, "SSH> server rejected, got: %s", line);
			goto tcp_cleanup;
		}
		pkg_debug(1, "SSH> server is: %s", line +4);
	} else {
		pkg_debug(1, "SSH> nothing to read, got: %s", line);
		goto tcp_cleanup;
	}
	retcode = EPKG_OK;
tcp_cleanup:
	if (retcode == EPKG_FATAL && repo->ssh != NULL) {
		fclose(repo->ssh);
		repo->ssh = NULL;
	}
	free(line);
	return (retcode);
}

static int
ssh_connect(struct pkg_repo *repo, struct url *u)
{
	char *line = NULL;
	size_t linecap = 0;
	int sshin[2];
	int sshout[2];
	xstring *cmd = NULL;
	char *cmdline;
	int retcode = EPKG_FATAL;
	const char *ssh_args;
	const char *argv[4];

	/* Use socket pair because pipe have blocking issues */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sshin) <0 ||
			socketpair(AF_UNIX, SOCK_STREAM, 0, sshout) < 0)
		return(EPKG_FATAL);

	repo->sshio.pid = fork();
	if (repo->sshio.pid == -1) {
		pkg_emit_errno("Cannot fork", "start_ssh");
		goto ssh_cleanup;
	}

	if (repo->sshio.pid == 0) {

		if (dup2(sshin[0], STDIN_FILENO) < 0 ||
				close(sshin[1]) < 0 ||
				close(sshout[0]) < 0 ||
				dup2(sshout[1], STDOUT_FILENO) < 0) {
			pkg_emit_errno("Cannot prepare pipes", "start_ssh");
			goto ssh_cleanup;
		}

		cmd = xstring_new();
		fputs("/usr/bin/ssh -e none -T ", cmd->fp);

		ssh_args = pkg_object_string(pkg_config_get("PKG_SSH_ARGS"));
		if (ssh_args != NULL)
			fprintf(cmd->fp, "%s ", ssh_args);
		if ((repo->flags & REPO_FLAGS_USE_IPV4) == REPO_FLAGS_USE_IPV4)
			fputs("-4 ", cmd->fp);
		else if ((repo->flags & REPO_FLAGS_USE_IPV6) == REPO_FLAGS_USE_IPV6)
			fputs("-6 ", cmd->fp);
		if (u->port > 0)
			fprintf(cmd->fp, "-p %d ", u->port);
		if (u->user[0] != '\0')
			fprintf(cmd->fp, "%s@", u->user);
		fprintf(cmd->fp, "%s pkg ssh", u->host);
		cmdline = xstring_get(cmd);
		pkg_debug(1, "Fetch: running '%s'", cmdline);
		argv[0] = _PATH_BSHELL;
		argv[1] = "-c";
		argv[2] = cmdline;
		argv[3] = NULL;

		if (sshin[0] != STDIN_FILENO)
			close(sshin[0]);
		if (sshout[1] != STDOUT_FILENO)
			close(sshout[1]);
		execvp(argv[0], __DECONST(char **, argv));
		/* NOT REACHED */
	}

	if (close(sshout[1]) < 0 || close(sshin[0]) < 0) {
		pkg_emit_errno("Failed to close pipes", "start_ssh");
		goto ssh_cleanup;
	}

	pkg_debug(1, "SSH> connected");

	repo->sshio.in = sshout[0];
	repo->sshio.out = sshin[1];
	set_nonblocking(repo->sshio.in);

	repo->ssh = funopen(repo, ssh_read, ssh_write, NULL, ssh_close);
	if (repo->ssh == NULL) {
		pkg_emit_errno("Failed to open stream", "start_ssh");
		goto ssh_cleanup;
	}

	if (getline(&line, &linecap, repo->ssh) > 0) {
		if (strncmp(line, "ok:", 3) != 0) {
			pkg_debug(1, "SSH> server rejected, got: %s", line);
			goto ssh_cleanup;
		}
		pkg_debug(1, "SSH> server is: %s", line +4);
	} else {
		pkg_debug(1, "SSH> nothing to read, got: %s", line);
		goto ssh_cleanup;
	}
	retcode = EPKG_OK;

ssh_cleanup:
	if (retcode == EPKG_FATAL && repo->ssh != NULL) {
		fclose(repo->ssh);
		repo->ssh = NULL;
	}
	free(line);
	return (retcode);
}

static int
pkgprotocol_open(struct pkg_repo *repo, struct url *u, off_t *sz,
    int (*proto_connect)(struct pkg_repo *, struct url *))
{
	char *line = NULL;
	size_t linecap = 0;
	size_t linelen;
	const char *errstr;
	int retcode = EPKG_FATAL;

	pkg_debug(1, "SSH> tcp_open");
	if (repo->ssh == NULL)
		retcode = proto_connect(repo, u);
	else
		retcode = EPKG_OK;

	if (retcode != EPKG_OK)
		return (retcode);

	pkg_debug(1, "SSH> get %s %" PRIdMAX "", u->doc, (intmax_t)u->ims_time);
	fprintf(repo->ssh, "get %s %" PRIdMAX "\n", u->doc, (intmax_t)u->ims_time);
	if ((linelen = getline(&line, &linecap, repo->ssh)) > 0) {
		if (line[linelen -1 ] == '\n')
			line[linelen -1 ] = '\0';

		pkg_debug(1, "SSH> recv: %s", line);
		if (strncmp(line, "ok:", 3) == 0) {
			*sz = strtonum(line + 4, 0, LONG_MAX, &errstr);
			if (errstr) {
				goto out;
			}

			if (*sz == 0) {
				retcode = EPKG_UPTODATE;
				goto out;
			}

			retcode = EPKG_OK;
			goto out;
		}
		if (strncmp(line, "ko:", 3) == 0) {
			retcode = EPKG_FATAL;
			goto out;
		}
	}

out:
	free(line);
	return (retcode);
}

int
tcp_open(struct pkg_repo *repo, struct url *u, off_t *sz)
{
	return (pkgprotocol_open(repo, u, sz, tcp_connect));
}

int
ssh_open(struct pkg_repo *repo, struct url *u, off_t *sz)
{
	return (pkgprotocol_open(repo, u, sz, ssh_connect));
}

static int
tcp_close(void *data)
{
	struct pkg_repo *repo = (struct pkg_repo *)data;

	write(repo->sshio.out, "quit\n", 5);
	close(repo->sshio.out);
	close(repo->sshio.in);
	repo->ssh = NULL;
	return (0);
}

static int
ssh_close(void *data)
{
	struct pkg_repo *repo = (struct pkg_repo *)data;
	int pstat;

	write(repo->sshio.out, "quit\n", 5);

	while (waitpid(repo->sshio.pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (EPKG_FATAL);
	}
	close(repo->sshio.out);
	close(repo->sshio.in);

	repo->ssh = NULL;

	return (WEXITSTATUS(pstat));
}

static int
ssh_writev(int fd, struct iovec *iov, int iovcnt)
{
	struct timeval now, timeout, delta;
	struct pollfd pfd;
	ssize_t wlen, total;
	int deltams;
	struct msghdr msg;

	memset(&pfd, 0, sizeof pfd);

	if (fetchTimeout) {
		pfd.fd = fd;
		pfd.events = POLLOUT | POLLERR;
		gettimeofday(&timeout, NULL);
		timeout.tv_sec += fetchTimeout;
	}

	total = 0;
	while (iovcnt > 0) {
		while (fetchTimeout && pfd.revents == 0) {
			gettimeofday(&now, NULL);
			if (!timercmp(&timeout, &now, >)) {
				errno = ETIMEDOUT;
				return (-1);
			}
			timersub(&timeout, &now, &delta);
			deltams = delta.tv_sec * 1000 +
				delta.tv_usec / 1000;
			errno = 0;
			pfd.revents = 0;
			while (poll(&pfd, 1, deltams) == -1) {
				if (errno == EINTR)
					continue;

				return (-1);
			}
		}
		errno = 0;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = iov;
		msg.msg_iovlen = iovcnt;

		wlen = sendmsg(fd, &msg, 0);
		if (wlen == 0) {
			errno = ECONNRESET;
			return (-1);
		}
		else if (wlen < 0)
			return (-1);

		total += wlen;

		while (iovcnt > 0 && wlen >= (ssize_t)iov->iov_len) {
			wlen -= iov->iov_len;
			iov++;
			iovcnt--;
		}

		if (iovcnt > 0) {
			iov->iov_len -= wlen;
			iov->iov_base = __DECONST(char *, iov->iov_base) + wlen;
		}
	}
	return (total);
}

static int
ssh_write(void *data, const char *buf, int l)
{
	struct pkg_repo *repo = (struct pkg_repo *)data;
	struct iovec iov;

	iov.iov_base = __DECONST(char *, buf);
	iov.iov_len = l;

	pkg_debug(1, "writing data");

	return (ssh_writev(repo->sshio.out, &iov, 1));
}

static int
ssh_read(void *data, char *buf, int len)
{
	struct pkg_repo *repo = (struct pkg_repo *) data;
	struct timeval now, timeout, delta;
	struct pollfd pfd;
	ssize_t rlen;
	int deltams;

	pkg_debug(2, "ssh: start reading");

	if (fetchTimeout > 0) {
		gettimeofday(&timeout, NULL);
		timeout.tv_sec += fetchTimeout;
	}

	deltams = -1;
	memset(&pfd, 0, sizeof pfd);
	pfd.fd = repo->sshio.in;
	pfd.events = POLLIN | POLLERR;

	for (;;) {
		rlen = read(pfd.fd, buf, len);
		pkg_debug(2, "read %jd", (intmax_t)rlen);
		if (rlen >= 0) {
			break;
		} else if (rlen == -1) {
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN) {
				pkg_emit_errno("timeout", "ssh");
				return (-1);
			}
		}

		/* only EAGAIN should get here */
		if (fetchTimeout > 0) {
			gettimeofday(&now, NULL);
			if (!timercmp(&timeout, &now, >)) {
				errno = ETIMEDOUT;
				return (-1);
			}
			timersub(&timeout, &now, &delta);
			deltams = delta.tv_sec * 1000 +
			    delta.tv_usec / 1000;
		}

		errno = 0;
		pfd.revents = 0;
		pkg_debug(1, "begin poll()");
		if (poll(&pfd, 1, deltams) < 0) {
			if (errno == EINTR)
				continue;
			return (-1);
		}
		pkg_debug(1, "end poll()");

	}

	pkg_debug(2, "ssh: have read %jd bytes", (intmax_t)rlen);

	return (rlen);
}
