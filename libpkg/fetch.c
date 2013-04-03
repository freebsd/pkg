/*-
 * Copyright (c) 2012-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/event.h>
#include <sys/time.h>

#include <ctype.h>
#include <fcntl.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fetch.h>
#include <pthread.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

static void
gethttpmirrors(struct pkg_fetch *fetch, const char *url) {
	FILE *f;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	struct http_mirror *m;
	struct url *u;

	if ((f = fetchGetURL(url, "")) == NULL)
		return;

	while ((linelen = getline(&line, &linecap, f)) > 0) {
		if (strncmp(line, "URL:", 4) == 0) {
			/* trim '\n' */
			if (line[linelen - 1] == '\n')
				line[linelen - 1 ] = '\0';

			line += 4;
			while (isspace(*line)) {
				line++;
			}
			if (*line == '\0')
				continue;

			if ((u = fetchParseURL(line)) != NULL) {
				m = malloc(sizeof(struct http_mirror));
				m->url = u;
				LL_APPEND(fetch->http, m);
			}
		}
	}
	fclose(f);
	return;
}

int
pkg_fetch_new(struct pkg_fetch **f)
{
	if ((*f = calloc(1, sizeof(struct pkg_fetch))) == NULL) {
		pkg_emit_errno("calloc", "fetch");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkg_fetch_free(struct pkg_fetch *f)
{
	if (f->ssh != NULL) {
		fprintf(f->ssh, "quit\n");
		pclose(f->ssh);
	}
	free(f);

	return (EPKG_OK);
}

int
pkg_fetch_file(const char *url, const char *dest, time_t t)
{
	int fd = -1;
	int retcode = EPKG_FATAL;
	struct pkg_fetch *f;

	if ((fd = open(dest, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644)) == -1) {
		pkg_emit_errno("open", dest);
		return(EPKG_FATAL);
	}

	pkg_fetch_new(&f);
	retcode = pkg_fetch_file_to_fd(f, url, fd, &t);
	pkg_fetch_free(f);

	if (t != 0) {
		struct timeval ftimes[2] = {
			{
			.tv_sec = t,
			.tv_usec = 0
			},
			{
			.tv_sec = t,
			.tv_usec = 0
			}
		};
		futimes(fd, ftimes);
	}

	close(fd);

	/* Remove local file if fetch failed */
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

static int
start_ssh(struct pkg_fetch *f, struct url *u, off_t *sz)
{
	char *line = NULL;
	ssize_t linecap = 0;
	size_t linelen;
	struct sbuf *cmd = NULL;
	const char *errstr;

	if (f->ssh == NULL) {
		cmd = sbuf_new_auto();
		sbuf_cat(cmd, "/usr/bin/ssh -e none -T ");
		if (u->port > 0)
			sbuf_printf(cmd, "-P %d ", u->port);
		if (u->user[0] != '\0')
			sbuf_printf(cmd, "%s@", u->user);
		sbuf_cat(cmd, u->host);
		sbuf_printf(cmd, " pkg ssh");
		sbuf_finish(cmd);
		if ((f->ssh = popen(sbuf_data(cmd), "r+")) == NULL) {
			pkg_emit_errno("popen", "ssh");
			sbuf_delete(cmd);
			return (EPKG_FATAL);
		}
		sbuf_delete(cmd);

		if (getline(&line, &linecap, f->ssh) > 0) {
			if (strncmp(line, "ok:", 3) != 0) {
				pclose(f->ssh);
				free(line);
				return (EPKG_FATAL);
			}
		} else {
			pclose(f->ssh);
			return (EPKG_FATAL);
		}
	}

	fprintf(f->ssh, "get %s %ld\n", u->doc, u->ims_time);
	if ((linelen = getline(&line, &linecap, f->ssh)) > 0) {
		if (line[linelen -1 ] == '\n')
			line[linelen -1 ] = '\0';
		if (strncmp(line, "ok:", 3) == 0) {
			*sz = strtonum(line + 4, 0, LONG_MAX, &errstr);
			if (errstr) {
				free(line);
				return (EPKG_FATAL);
			}

			if (*sz == 0) {
				free(line);
				return (EPKG_UPTODATE);
			}

			free(line);
			return (EPKG_OK);
		}
	}
	free(line);
	return (EPKG_FATAL);
}

int
pkg_fetch_file_to_fd(struct pkg_fetch *f, const char *url, int dest, time_t *t)
{
	FILE *remote = NULL;
	struct url *u;
	struct url_stat st;
	off_t done = 0;
	off_t r;

	int64_t max_retry, retry;
	int64_t fetch_timeout;
	time_t begin_dl;
	time_t now;
	time_t last = 0;
	char buf[10240];
	char *doc = NULL;
	char docpath[MAXPATHLEN];
	int retcode = EPKG_OK;
	bool srv = false;
	bool http = false;
	char zone[MAXHOSTNAMELEN + 13];
	struct dns_srvinfo *srv_current = NULL;
	struct http_mirror *http_current = NULL;
	const char *mt;
	off_t sz = 0;
	int kq = -1;
	struct kevent e, ev;

	if (pkg_config_int64(PKG_CONFIG_FETCH_RETRY, &max_retry) == EPKG_FATAL)
		max_retry = 3;

	if (pkg_config_int64(PKG_CONFIG_FETCH_TIMEOUT, &fetch_timeout) == EPKG_FATAL)
		fetch_timeout = 30;

	fetchTimeout = (int) fetch_timeout;

	retry = max_retry;

	u = fetchParseURL(url);
	if (t != NULL)
		u->ims_time = *t;

	if (strcmp(u->scheme, "ssh") == 0) {
		if ((retcode = start_ssh(f, u, &sz)) != EPKG_OK)
			goto cleanup;
		remote = f->ssh;
		kq = kqueue();
		EV_SET(&e, fileno(f->ssh), EVFILT_READ, EV_ADD, 0, 0, 0);
		kevent(kq, &e, 1, NULL, 0, NULL);
		fcntl(fileno(f->ssh), F_SETFL, O_NONBLOCK);
	}

	doc = u->doc;
	while (remote == NULL) {
		if (retry == max_retry) {
			pkg_config_string(PKG_CONFIG_MIRRORS, &mt);
			if (mt != NULL && strncasecmp(mt, "srv", 3) == 0 &&
			    (strncmp(u->scheme, "http", 4) == 0
			     || strcmp(u->scheme, "ftp") == 0)) {
				srv = true;
				snprintf(zone, sizeof(zone),
				    "_%s._tcp.%s", u->scheme, u->host);
				if (f->srv == NULL)
					f->srv = dns_getsrvinfo(zone);
				srv_current = f->srv;
			} else if (mt != NULL && strncasecmp(mt, "http", 4) == 0 &&
			           strncmp(u->scheme, "http", 4) == 0) {
				http = true;
				snprintf(zone, sizeof(zone),
				    "%s://%s", u->scheme, u->host);
				if (f->http == NULL)
					gethttpmirrors(f, zone);
				http_current = f->http;
			}
		}

		if (srv && f->srv != NULL)
			strlcpy(u->host, srv_current->host, sizeof(u->host));
		else if (http && f->http != NULL) {
			strlcpy(u->scheme, http_current->url->scheme, sizeof(u->scheme));
			strlcpy(u->host, http_current->url->host, sizeof(u->host));
			snprintf(docpath, MAXPATHLEN, "%s%s", http_current->url->doc, doc);
			u->doc = docpath;
			u->port = http_current->url->port;
		}

		remote = fetchXGet(u, &st, "i");
		if (remote == NULL) {
			if (fetchLastErrCode == FETCH_OK) {
				retcode = EPKG_UPTODATE;
				goto cleanup;
			}
			--retry;
			if (retry <= 0) {
				pkg_emit_error("%s: %s", url,
				    fetchLastErrString);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			if (srv && f->srv != NULL) {
				srv_current = srv_current->next;
				if (srv_current == NULL)
					srv_current = f->srv;
			} else if (http && f->http != NULL) {
				http_current = f->http->next;
				if (http_current == NULL)
					http_current = f->http;
			} else {
				sleep(1);
			}
		}
	}
	if (strcmp(u->scheme, "ssh") != 0) {
		if (t != NULL) {
			if (st.mtime < *t) {
				retcode = EPKG_UPTODATE;
				goto cleanup;
			} else
				*t = st.mtime;
		}
		sz = st.size;
	}

	begin_dl = time(NULL);
	while (done < sz) {
		if (kq == - 1) {
			if ((r = fread(buf, 1, sizeof(buf), remote)) < 1)
				break;
		} else {
			kevent(kq, &e, 1, &ev, 1, NULL);
			if (ev.data == 0)
				break;
			size_t size = (size_t)ev.data;
			if (size > sizeof(buf))
				size = sizeof(buf);
			if ((r = fread(buf, 1, size, remote)) < 1)
				break;
		}

		if (write(dest, buf, r) != r) {
			pkg_emit_errno("write", "");
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		done += r;
		now = time(NULL);
		/* Only call the callback every second */
		if (now > last || done == sz) {
			pkg_emit_fetching(url, sz, done, (now - begin_dl));
			last = now;
		}
	}

	if (done < sz) {
		pkg_emit_error("An error occured while fetching package");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (strcmp(u->scheme, "ssh") != 0 && ferror(remote)) {
		pkg_emit_error("%s: %s", url, fetchLastErrString);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	cleanup:

	if (strcmp(u->scheme, "ssh") != 0 && remote != NULL) {
		fclose(remote);
	} else {
		EV_SET(&e, fileno(f->ssh), EVFILT_READ, EV_DELETE, 0, 0, 0);
		kevent(kq, &e, 1, NULL, 0, NULL);
		int flags = fcntl(fileno(f->ssh), F_GETFL, 0);
		flags &= ~O_NONBLOCK;
		fcntl(fileno(f->ssh), F_SETFL, flags);
	}

	if (kq != -1)
		close(kq);

	/* restore original doc */
	u->doc = doc;

	fetchFreeURL(u);

	return (retcode);
}
