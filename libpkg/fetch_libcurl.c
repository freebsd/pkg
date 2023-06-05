/*-
 * Copyright (c) 2023 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

struct curl_userdata {
	int fd;
	CURL *cl;
	FILE *fh;
	size_t size;
	size_t totalsize;
	bool started;
	const char *url;
	long response;
};

static size_t
curl_write_cb(char *data, size_t size, size_t nmemb, void *userdata)
{
	struct curl_userdata *d = (struct curl_userdata *)userdata;
	size_t written;

	written = fwrite(data, size, nmemb, d->fh);
	d->size += written;

	return (written);
}

static size_t
curl_parseheader_cb(void *ptr __unused, size_t size, size_t nmemb, void *userdata)
{
	struct curl_userdata *d = (struct curl_userdata *)userdata;

	if (!d->started) {
		pkg_emit_fetch_begin(d->url);
		pkg_emit_progress_start(NULL);
		d->started = true;
	}

	curl_easy_getinfo(d->cl, CURLINFO_RESPONSE_CODE, &d->response);

	return (size *nmemb);

}

static size_t
curl_progress_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal __unused, curl_off_t ulnow __unused)
{
	struct curl_userdata *d = (struct curl_userdata *)userdata;

	if (d->response != 200)
		return (0);

	pkg_emit_progress_tick(dlnow, dltotal);
	return (0);
}


int
curl_open(struct pkg_repo *repo, const char *u __unused,
    size_t *sz __unused, time_t *t __unused)
{
	CURLM *cm;
	pkg_debug(1, "curl_open");

	if (repo->fetch_priv != NULL)
		return (EPKG_OK);

	curl_global_init(CURL_GLOBAL_ALL);
	cm = curl_multi_init();
	curl_multi_setopt(cm, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
	curl_multi_setopt(cm, CURLMOPT_MAX_HOST_CONNECTIONS, 1);
	/* TODO: Later for parallel fetching */
	/*curl_multi_setopt(cm, CURLMOPT_MAX_HOST_TOTAL_CONNECTIONS, 4);*/
	repo->fetch_priv = cm;

	return (EPKG_OK);
}

int
curl_fetch(struct pkg_repo *repo, int dest, const char *url, off_t sz, off_t offset __unused, time_t *t)
{
	CURL *cl;
	CURLM *cm = NULL;
	struct curl_userdata data = { 0 };
	int still_running = 1;
	CURLMsg *msg;
	int msgs_left;
	int retcode = EPKG_OK;

	cm = (CURLM *)repo->fetch_priv;

	data.fh = fdopen(dup(dest), "w");
	if (data.fh == NULL)
		return (EPKG_FATAL);
	data.totalsize = sz;
	data.url = url;

	pkg_debug(1, "curl> fetching %s\n", url);
	cl = curl_easy_init();
	data.cl = cl;
	curl_easy_setopt(cl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(cl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(cl, CURLOPT_WRITEDATA, &data);
	curl_easy_setopt(cl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(cl, CURLOPT_PRIVATE, &data);
	curl_easy_setopt(cl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
	curl_easy_setopt(cl, CURLOPT_XFERINFODATA, &data);
	curl_easy_setopt(cl, CURLOPT_URL, url); /* TODO handle mirrors */
	curl_easy_setopt(cl, CURLOPT_TIMEVALUE, (long)*t);
	curl_easy_setopt(cl, CURLOPT_TIMECONDITION, (long)CURL_TIMECOND_IFMODSINCE);
	curl_easy_setopt(cl, CURLOPT_HEADERFUNCTION, curl_parseheader_cb);
	curl_easy_setopt(cl, CURLOPT_HEADERDATA, &data);
	if (repo->fetcher->timeout > 0)
		curl_easy_setopt(cl, CURLOPT_TIMEOUT, repo->fetcher->timeout);

	//curl_easy_setopt(cl, CURLOPT_MAXFILESIZE_LARGE, *sz);
	/* compat with libfetch */
	if (getenv("SSL_NO_VERFIRY_PEER") != NULL)
		curl_easy_setopt(cl, CURLOPT_SSL_VERIFYPEER, 0L);
	if (getenv("SSL_NO_VERIFY_HOSTNAME") != NULL)
		curl_easy_setopt(cl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_multi_add_handle(cm, cl);

	while(still_running) {
		CURLMcode mc = curl_multi_perform(cm, &still_running);

		if(still_running)
			/* wait for activity, timeout or "nothing" */
			mc = curl_multi_poll(cm, NULL, 0, 1000, NULL);

		if(mc)
			break;
	}
	while((msg = curl_multi_info_read(cm, &msgs_left))) {
		fclose(data.fh);
		if(msg->msg == CURLMSG_DONE) {
			CURL *eh = msg->easy_handle;
			long rc = 0;
			curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &rc);
			if (rc == 304)
				retcode = EPKG_UPTODATE;
			else if (rc != 200) {
				pkg_emit_error("An error occured while fetching package");
				retcode = EPKG_FATAL;
			}
		} else {
			pkg_emit_error("An error occured while fetching package");
		}
	}

	curl_multi_remove_handle(cm, cl);
	curl_easy_cleanup(cl);

	return (retcode);
}

int
curl_cleanup(struct pkg_repo *repo)
{
	CURLM *cm;

	if (repo->fetch_priv == NULL)
		return (EPKG_OK);
	cm = repo->fetch_priv;
	curl_multi_cleanup(cm);
	repo->fetch_priv = NULL;
	return (EPKG_OK);
}
