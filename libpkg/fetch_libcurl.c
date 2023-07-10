/*-
 * Copyright (c) 2023 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
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
#include <stdlib.h>
#include <curl/curl.h>
#include <ctype.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "private/fetch.h"

/*
 * The choice of 2KB/s is arbitrary; at some point this should be configurable.
 */
#define	LIBPKG_SPEED_LIMIT	(2 * 1024)	/* bytes per second */

struct curl_repodata {
	CURLM *cm;
	CURLU *url;
};

struct curl_userdata {
	int fd;
	CURL *cl;
	FILE *fh;
	size_t size;
	size_t totalsize;
	size_t content_length;
	bool started;
	const char *url;
	long response;
};

struct http_mirror {
	CURLU *url;
	struct http_mirror *next;
};

static
void dump(const char *text,
          FILE *stream, unsigned char *ptr, size_t size)
{
  size_t i;
  size_t c;

  unsigned int width = 0x40;

  fprintf(stream, "%s, %10.10lu bytes (0x%8.8lx)\n",
          text, (unsigned long)size, (unsigned long)size);

  for(i = 0; i<size; i += width) {

    fprintf(stream, "%4.4lx: ", (unsigned long)i);

    for(c = 0; (c < width) && (i + c < size); c++) {
      /* check for 0D0A; if found, skip past and start a new line of output */
      if((i + c + 1 < size) && ptr[i + c] == 0x0D &&
         ptr[i + c + 1] == 0x0A) {
        i += (c + 2 - width);
        break;
      }
      fprintf(stream, "%c",
              (ptr[i + c] >= 0x20) && (ptr[i + c]<0x80)?ptr[i + c]:'.');
      /* check again for 0D0A, to avoid an extra \n if it's at width */
      if((i + c + 2 < size) && ptr[i + c + 1] == 0x0D &&
         ptr[i + c + 2] == 0x0A) {
        i += (c + 3 - width);
        break;
      }
    }
    fputc('\n', stream); /* newline */
  }
  fflush(stream);
}

static
int my_trace(CURL *handle, curl_infotype type,
             char *data, size_t size,
             void *userp __unused)
{
  const char *text;
  (void)handle; /* prevent compiler warning */

  switch(type) {
  case CURLINFO_TEXT:
    fprintf(stderr, "== Info: %s", data);
    /* FALLTHROUGH */
  default: /* in case a new one is introduced to shock us */
    return 0;

  case CURLINFO_HEADER_OUT:
    text = "=> Send header";
    break;
  case CURLINFO_DATA_OUT:
    text = "=> Send data";
    break;
  case CURLINFO_SSL_DATA_OUT:
    text = "=> Send SSL data";
    break;
  case CURLINFO_HEADER_IN:
    text = "<= Recv header";
    break;
  case CURLINFO_DATA_IN:
    text = "<= Recv data";
    break;
  case CURLINFO_SSL_DATA_IN:
    text = "<= Recv SSL data";
    break;
  }

  dump(text, stderr, (unsigned char *)data, size);
  return 0;
}

static long
curl_do_fetch(struct curl_userdata *data, CURL *cl, struct curl_repodata *cr)
{
	int still_running = 1;
	CURLMsg *msg;
	int msg_left;

	curl_easy_setopt(cl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(cl, CURLOPT_PRIVATE, &data);

	if (ctx.debug_level > 0)
		curl_easy_setopt(cl, CURLOPT_VERBOSE, 1L);
	if (ctx.debug_level > 1)
		curl_easy_setopt(cl, CURLOPT_DEBUGFUNCTION, my_trace);

	/* compat with libfetch */
	if (getenv("SSL_NO_VERIFY_PEER") != NULL)
		curl_easy_setopt(cl, CURLOPT_SSL_VERIFYPEER, 0L);
	if (getenv("SSL_NO_VERIFY_HOSTNAME") != NULL)
		curl_easy_setopt(cl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_multi_add_handle(cr->cm, cl);

	while(still_running) {
		CURLMcode mc = curl_multi_perform(cr->cm, &still_running);

		if(still_running)
			/* wait for activity, timeout or "nothing" */
			mc = curl_multi_poll(cr->cm, NULL, 0, 1000, NULL);

		if(mc)
			break;
	}

	while ((msg = curl_multi_info_read(cr->cm, &msg_left))) {
		if (msg->msg == CURLMSG_DONE) {
			if (msg->data.result == CURLE_ABORTED_BY_CALLBACK)
				return (-1); // FIXME: more clear return code?
			CURL *eh = msg->easy_handle;
			long rc = 0;
			curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &rc);
			return (rc);
		}
	}
	return (0);
}

static size_t
curl_write_cb(char *data, size_t size, size_t nmemb, void *userdata)
{
	struct curl_userdata *d = (struct curl_userdata *)userdata;
	size_t written;

	written = fwrite(data, size, nmemb, d->fh);
	d->size += written;

	return (written);
}

static struct http_mirror *
http_getmirrors(struct pkg_repo *r, struct curl_repodata *cr)
{
	CURL *cl;
	struct curl_userdata data = { 0 };
	char *buf = NULL, *walk, *line;
	size_t cap = 0;
	struct http_mirror *m, *mirrors = NULL;
	CURLU *url;
	pkg_debug(1, "CURL> fetching http mirror list if any");

	cl = curl_easy_init();
	data.fh = open_memstream(& buf, &cap);
	data.cl = cl;

	curl_easy_setopt(cl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(cl, CURLOPT_WRITEDATA, &data);
	curl_easy_setopt(cl, CURLOPT_MAXFILESIZE_LARGE, 1048576L);
	curl_easy_setopt(cl, CURLOPT_URL, r->url);
	curl_easy_setopt(cl, CURLOPT_NOPROGRESS, 1L);
	data.url = r->url;
	if (ctx.ip == IPV4)
		curl_easy_setopt(cl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	if (ctx.ip == IPV6)
		curl_easy_setopt(cl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
	curl_do_fetch(&data, cl, cr);
	fclose(data.fh);
	walk = buf;
	while ((line = strsep(&walk, "\n\r")) != NULL) {
		if (strncmp(line, "URL:", 4) != 0)
			continue;
		line += 4;
		while (isspace(*line))
			line++;
		if (*line == '\0')
			continue;
		url = curl_url();
		if (curl_url_set(url, CURLUPART_URL, line, 0)) {
			curl_url_cleanup(url);
			pkg_emit_error("Invalid mirror url: '%s'", line);
			continue;
		}
		m = xmalloc(sizeof(*m));
		m->url = url;
		pkg_debug(1, "CURL> appending an http mirror: %s", line);
		LL_APPEND(mirrors, m);
	}
	free(buf);

	return (mirrors);
}

static size_t
curl_parseheader_cb(void *ptr __unused, size_t size, size_t nmemb, void *userdata)
{
	struct curl_userdata *d = (struct curl_userdata *)userdata;

	curl_easy_getinfo(d->cl, CURLINFO_RESPONSE_CODE, &d->response);
	if (d->response == 404)
		return (CURLE_WRITE_ERROR);
	if (d->response == 200 && !d->started) {
		pkg_emit_fetch_begin(d->url);
		pkg_emit_progress_start(NULL);
		d->started = true;
	}

	return (size *nmemb);
}

static int
curl_progress_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal __unused, curl_off_t ulnow __unused)
{
	struct curl_userdata *d = (struct curl_userdata *)userdata;

	if (d->response != 200)
		return (0);

	return pkg_emit_progress_tick(dlnow, dltotal);
}

int
curl_open(struct pkg_repo *repo, struct fetch_item *fi __unused)
{
	struct curl_repodata *cr;
	pkg_debug(1, "curl_open");

	if (repo->fetch_priv != NULL)
		return (EPKG_OK);

	curl_global_init(CURL_GLOBAL_ALL);
	cr = xcalloc(1, sizeof(*cr));
	cr->cm = curl_multi_init();
	curl_multi_setopt(cr->cm, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
	curl_multi_setopt(cr->cm, CURLMOPT_MAX_HOST_CONNECTIONS, 1);
	/* TODO: Later for parallel fetching */
	/*curl_multi_setopt(cm, CURLMOPT_MAX_HOST_TOTAL_CONNECTIONS, 4);*/
	repo->fetch_priv = cr;
	if (repo->mirror_type == SRV && repo->srv == NULL) {
		int urloff = 0;
		cr->url = curl_url();
		if (strncasecmp(repo->url, "pkg+", 4) == 0)
			urloff = 4;
		CURLUcode c = curl_url_set(cr->url, CURLUPART_URL, repo->url + urloff, 0);
		if (c) {
			pkg_emit_error("impossible to parse url: '%s'", repo->url);
			return (EPKG_FATAL);
		}

		char *zone;
		char *host = NULL, *scheme = NULL;
		curl_url_get(cr->url, CURLUPART_HOST, &host, 0);
		curl_url_get(cr->url, CURLUPART_SCHEME, &scheme, 0);
		xasprintf(&zone, "_%s._tcp.%s", scheme, host);
		repo->srv = dns_getsrvinfo(zone);
		free(zone);
		free(host);
		free(scheme);
		if (repo->srv == NULL) {
			pkg_emit_error("No SRV record found for the "
			    "repo '%s'", repo->name);
			repo->mirror_type = NOMIRROR;
		}
	}
	if (repo->mirror_type == HTTP && repo->http == NULL) {
		if (strncasecmp(repo->url, "pkg+", 4) == 0) {
			pkg_emit_error("invalid for http mirror mechanism "
			   "scheme '%s'", repo->url);
			return (EPKG_FATAL);
		}
		cr->url = curl_url();
		CURLUcode c = curl_url_set(cr->url, CURLUPART_URL, repo->url, 0);
		if (c) {
			pkg_emit_error("impossible to parse url: '%s'", repo->url);
			return (EPKG_FATAL);
		}
		repo->http = http_getmirrors(repo, cr);
		if (repo->http == NULL) {
			pkg_emit_error("No HTTP mirrors founds for the repo "
			    "'%s'", repo->name);
			repo->mirror_type = NOMIRROR;
		}
	}

	return (EPKG_OK);
}

int
curl_fetch(struct pkg_repo *repo, int dest, struct fetch_item *fi)
{
	CURL *cl;
	CURLU *hu = NULL;
	struct curl_userdata data = { 0 };
	int64_t retry;
	int retcode = EPKG_OK;
	struct dns_srvinfo *srv_current = NULL;
	struct http_mirror *http_current = NULL;
	char *urlpath = NULL;
	const char *relpath = NULL;
	const char *userpasswd = get_http_auth();
	const char *http_proxy = getenv("HTTP_PROXY");
	const char *sslkey = getenv("SSL_CLIENT_KEY_FILE");
	const char *sslcert = getenv("SSL_CLIENT_CERT_FILE");

	struct curl_repodata *cr = (struct curl_repodata *)repo->fetch_priv;

	data.fh = fdopen(dup(dest), "w");
	if (data.fh == NULL)
		return (EPKG_FATAL);
	data.totalsize = fi->size;
	data.url = fi->url;

	pkg_debug(1, "curl> fetching %s\n", fi->url);
	retry = pkg_object_int(pkg_config_get("FETCH_RETRY"));
	if (repo->mirror_type == SRV || repo->mirror_type == HTTP) {
		CURLU *cu = curl_url();
		curl_url_set(cu, CURLUPART_URL, fi->url, 0);
		curl_url_get(cu, CURLUPART_PATH, &urlpath, 0);
		if (urlpath != NULL && repo->mirror_type == SRV)
			curl_url_set(cr->url, CURLUPART_PATH, urlpath, 0);
		if (urlpath != NULL && repo->mirror_type == HTTP) {
			CURLU *ru = curl_url();
			char *doc = NULL;
			curl_url_set(ru, CURLUPART_URL, repo->url, 0);
			curl_url_get(ru, CURLUPART_PATH, &doc, 0);
			relpath = urlpath;
			if (doc != NULL)
				relpath += strlen(doc);
			free(doc);
			curl_url_cleanup(ru);
		}
		curl_url_cleanup(cu);
	}

retry:
	cl = curl_easy_init();
	data.cl = cl;
	if (repo->mirror_type == SRV) {
		char *portstr;
		if (srv_current != NULL)
			srv_current = srv_current->next;
		if (srv_current == NULL)
			srv_current = repo->srv;
		curl_url_set(cr->url, CURLUPART_HOST, srv_current->host, 0);
		xasprintf(&portstr, "%d", srv_current->port);
		curl_url_set(cr->url, CURLUPART_PORT, portstr, 0);
		free(portstr);
		curl_easy_setopt(cl, CURLOPT_CURLU, cr->url);
	} else if (repo->mirror_type == HTTP) {
		if (http_current != NULL)
			http_current = http_current->next;
		if (http_current == NULL)
			http_current = repo->http;
		char *doc = NULL;
		char *p = NULL;
		const char *path = relpath;;
		curl_url_cleanup(hu);
		hu = curl_url_dup(http_current->url);
		curl_url_get(hu, CURLUPART_PATH, &doc, 0);
		if (doc != NULL) {
			xasprintf(&p, "%s/%s", doc, relpath);
			path = p;
		}
		curl_url_set(hu, CURLUPART_PATH, path, 0);
		free(p);
		char *lurl;
		curl_url_get(hu, CURLUPART_URL, &lurl, 0);
		pkg_debug(1, "CURL> new http mirror url: %s", lurl);
		curl_easy_setopt(cl, CURLOPT_CURLU, hu);
	} else {
		pkg_debug(1, "CURL> No mirror set url to %s\n", fi->url);
		curl_easy_setopt(cl, CURLOPT_URL, fi->url);
	}
	if (ctx.debug_level > 0) {
		const char *lurl = NULL;
		curl_easy_getinfo(cl, CURLINFO_EFFECTIVE_URL, &lurl);
		pkg_debug(1, "CURL> attempting to fetch from %s, left retry %ld\n",
				lurl, retry);
	}
	if (userpasswd != NULL) {
		curl_easy_setopt(cl, CURLOPT_HTTPAUTH, (long)CURLAUTH_ANY);
		curl_easy_setopt(cl, CURLOPT_USERPWD, userpasswd);
	}
	if (http_proxy != NULL)
		curl_easy_setopt(cl, CURLOPT_PROXY, http_proxy);
	if (sslkey != NULL)
		curl_easy_setopt(cl, CURLOPT_SSLKEY, sslkey);
	if (sslcert != NULL)
		curl_easy_setopt(cl, CURLOPT_SSLCERT, sslcert);

	if (repo->ip == IPV4)
		curl_easy_setopt(cl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	if (repo->ip == IPV6)
		curl_easy_setopt(cl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
	curl_easy_setopt(cl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(cl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(cl, CURLOPT_WRITEDATA, &data);
	curl_easy_setopt(cl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
	curl_easy_setopt(cl, CURLOPT_XFERINFODATA, &data);
	curl_easy_setopt(cl, CURLOPT_HEADERFUNCTION, curl_parseheader_cb);
	curl_easy_setopt(cl, CURLOPT_HEADERDATA, &data);
	curl_easy_setopt(cl, CURLOPT_TIMEVALUE, (long)fi->mtime);
	curl_easy_setopt(cl, CURLOPT_TIMECONDITION, (long)CURL_TIMECOND_IFMODSINCE);
	if (repo->fetcher->timeout > 0) {
		curl_easy_setopt(cl, CURLOPT_CONNECTTIMEOUT, repo->fetcher->timeout);

		curl_easy_setopt(cl, CURLOPT_LOW_SPEED_LIMIT, LIBPKG_SPEED_LIMIT);
		curl_easy_setopt(cl, CURLOPT_LOW_SPEED_TIME, repo->fetcher->timeout);
	}

	long rc = curl_do_fetch(&data, cl, cr);
	time_t t;
	curl_easy_getinfo(cl, CURLINFO_FILETIME_T, &t);
	curl_multi_remove_handle(cr->cm, cl);
	curl_easy_cleanup(cl);
	if (rc == 304) {
		retcode = EPKG_UPTODATE;
	} else if (rc == -1) {
		retcode = EPKG_CANCEL;
	} else if (rc != 200) {
		--retry;
		if (retry <= 0 || (rc == 404 && repo->mirror_type == NOMIRROR)) {
			pkg_emit_error("An error occured while fetching package");
			retcode = EPKG_FATAL;
		} else
			goto retry;
	}

	fi->mtime = t;
	fclose(data.fh);
	free(urlpath);
	curl_url_cleanup(hu);

	return (retcode);
}

void
curl_cleanup(struct pkg_repo *repo)
{
	struct curl_repodata *cr;

	if (repo->fetch_priv == NULL)
		return;
	cr = repo->fetch_priv;
	curl_multi_cleanup(cr->cm);
	if (cr->url != NULL)
		curl_url_cleanup(cr->url);
	repo->fetch_priv = NULL;
	return;
}
