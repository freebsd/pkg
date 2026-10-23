/*
 * Copyright (c) 2026 FreeBSD Foundation
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <fetch.h>
#include "pkg.h"
#include "private/pkg.h"
#include "private/fetch.h"
#include "private/event.h"

struct host {
	char *name;
	uint64_t limit;
	size_t active;
	unsigned int failures, generation;
	double retry_at;
};
struct endpoint {
	struct pkg_repo *repo;
	struct pkg_fetch_endpoint mirror;
	size_t host;
};
enum job_state { JOB_PENDING, JOB_ACTIVE, JOB_COMPLETE };

struct job {
	struct pkg *pkg;
	unsigned int *attempts, redirects, generation;
	enum job_state state;
	size_t endpoint, host;
	char *redirect, *target;
	char error[512];
};
struct pkg_fetch_plan {
	vec_t(struct host) hosts;
	vec_t(struct endpoint) endpoints;
	vec_t(size_t) active;
	struct job *jobs;
	char **paths;
	size_t count, cursor;
	uint64_t default_limit;
	unsigned int retries;
	double waiting_since;
};

static double
monotime(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec + ts.tv_nsec / 1e9);
}

static char *
hostname_key(const char *name)
{
	char *key = xstrdup(name);
	size_t len = strlen(key);
	while (len && key[len - 1] == '.')
		key[--len] = '\0';
	for (size_t i = 0; i < len; i++)
		key[i] = tolower((unsigned char)key[i]);
	struct in6_addr address;
	char canonical[INET6_ADDRSTRLEN];
	if (inet_pton(AF_INET6, key, &address) == 1 &&
	    inet_ntop(AF_INET6, &address, canonical, sizeof(canonical)) != NULL) {
		free(key);
		key = xstrdup(canonical);
	}
	return (key);
}

static int
host_add(struct pkg_fetch_plan *p, const char *url, size_t *index)
{
	struct url *u = fetchParseURL(url);
	struct host h = {0};
	const ucl_object_t *settings = pkg_config_get("FETCH_HOSTS"), *entry, *value;
	ucl_object_iter_t it = NULL;

	if (u == NULL)
		return (EPKG_FATAL);
	if (!u->host[0] && strcmp(u->scheme, "file") != 0) {
		fetchFreeURL(u);
		return (EPKG_FATAL);
	}
	h.name = hostname_key(strcmp(u->scheme, "file") == 0 ? "" : u->host);
	fetchFreeURL(u);
	for (*index = 0; *index < p->hosts.len; (*index)++)
		if (strcmp(p->hosts.d[*index].name, h.name) == 0) {
			free(h.name);
			return (EPKG_OK);
		}
	h.limit = h.name[0] ? p->default_limit : UINT64_MAX;
	while ((entry = ucl_iterate_object(settings, &it, true))) {
		char *key = hostname_key(ucl_object_key(entry));
		bool match = strcmp(key, h.name) == 0;
		free(key);
		if (!match)
			continue;
		value = ucl_object_find_key(entry, "fetch_workers");
		if (value != NULL)
			h.limit = ucl_object_toint(value);
	}
	vec_push(&p->hosts, h);
	return (EPKG_OK);
}

static bool
validate_hosts(void)
{
	const ucl_object_t *settings = pkg_config_get("FETCH_HOSTS"), *entry, *value;
	ucl_object_iter_t it = NULL, fields;
	c_charv_t keys = vec_init();
	bool valid = true;

	while ((entry = ucl_iterate_object(settings, &it, true))) {
		char *key = hostname_key(ucl_object_key(entry));
		struct in6_addr address;
		if (!*key || strpbrk(key, "/@ \t\n") != NULL ||
		    (strchr(key, ':') && inet_pton(AF_INET6, key, &address) != 1) ||
		    entry->type != UCL_OBJECT)
			valid = false;
		vec_foreach(keys, i)
			if (strcmp(keys.d[i], key) == 0)
				valid = false;
		vec_push(&keys, key);
		fields = NULL;
		while ((value = ucl_iterate_object(entry, &fields, true))) {
			if (strcmp(ucl_object_key(value), "fetch_workers") != 0 ||
			    value->type != UCL_INT || ucl_object_toint(value) < 1)
				valid = false;
		}
	}
	vec_foreach(keys, i)
		free((void *)keys.d[i]);
	vec_free(&keys);
	if (!valid)
		pkg_emit_error("FETCH_HOSTS requires unique hostnames and positive integer fetch_workers values");
	return (valid);
}

void
pkg_fetch_plan_free(struct pkg_fetch_plan *p)
{
	if (p == NULL)
		return;
	vec_foreach(p->hosts, i)
		free(p->hosts.d[i].name);
	vec_foreach(p->endpoints, i)
		free(p->endpoints.d[i].mirror.url);
	for (size_t i = 0; i < p->count; i++) {
		free(p->jobs[i].attempts);
		free(p->jobs[i].redirect);
		free(p->jobs[i].target);
	}
	free(p->jobs);
	vec_free(&p->hosts);
	vec_free(&p->endpoints);
	vec_free(&p->active);
	free(p);
}

struct pkg_fetch_plan *
pkg_fetch_plan_new(struct pkg **pkgs, char **paths, size_t count)
{
	struct pkg_fetch_plan *p = xcalloc(1, sizeof(*p));
	int64_t limit = pkg_object_int(pkg_config_get("FETCH_WORKERS_PER_HOST"));
	int64_t retries = pkg_object_int(pkg_config_get("FETCH_RETRY"));

	if (limit < 1) {
		pkg_emit_error("FETCH_WORKERS_PER_HOST must be greater than zero");
		free(p);
		return (NULL);
	}
	if (!validate_hosts()) {
		free(p);
		return (NULL);
	}
	p->count = count;
	p->paths = paths;
	p->default_limit = limit;
	p->retries = MIN(MAX(retries, 1), UINT_MAX);
	p->jobs = xcalloc(count, sizeof(*p->jobs));
	for (size_t i = 0; i < count; i++) {
		struct pkg_repo *repo = pkgs[i]->repo;
		bool known = false;
		p->jobs[i].pkg = pkgs[i];
		if (repo == NULL || repo->url == NULL || pkgs[i]->repopath == NULL)
			goto fail;
		vec_foreach(p->endpoints, n)
			if (p->endpoints.d[n].repo == repo)
				known = true;
		if (known)
			continue;
		pkg_fetch_endpoints mirrors = vec_init();
		if (pkg_fetch_discover(repo, &mirrors) != EPKG_OK) {
			vec_free(&mirrors);
			goto fail;
		}
		vec_foreach(mirrors, n) {
			struct endpoint ep = { .repo = repo, .mirror = mirrors.d[n] };
			if (host_add(p, ep.mirror.url, &ep.host) != EPKG_OK) {
				for (size_t k = n; k < mirrors.len; k++)
					free(mirrors.d[k].url);
				vec_free(&mirrors);
				goto fail;
			}
			vec_push(&p->endpoints, ep);
		}
		vec_free(&mirrors);
	}
	for (size_t i = 0; i < count; i++)
		p->jobs[i].attempts = xcalloc(p->endpoints.len, sizeof(unsigned int));
	return (p);
fail:
	pkg_fetch_plan_free(p);
	return (NULL);
}

/* Return one runnable job, wait, or fail when no candidate can recover. */
int
pkg_fetch_plan_next(struct pkg_fetch_plan *p, size_t *number, const char **target)
{
	double now = monotime();
	bool possible = false, active = p->active.len > 0;

	for (size_t n = 0; n < p->count; n++) {
		size_t i = (p->cursor + n) % p->count, selected = SIZE_MAX;
		struct job *j = &p->jobs[i];
		unsigned int priority = UINT_MAX, weight = 0, zero_weights = 0;
		bool busy = false;

		if (j->state == JOB_ACTIVE) {
			active = true;
			continue;
		}
		if (j->state == JOB_COMPLETE)
			continue;
		vec_foreach(p->active, k)
			if (strcmp(p->paths[i], p->paths[p->active.d[k]]) == 0)
				busy = true;
		if (busy) {
			possible = true;
			continue;
		}
		if (j->redirect != NULL) {
			struct host *h = &p->hosts.d[j->host];
			if (h->failures >= p->retries)
				continue;
			possible = true;
			if (h->active >= h->limit || now < h->retry_at || (h->failures && h->active))
				continue;
		} else {
			/* Occupied primary SRV targets do not promote backup priorities. */
			vec_foreach(p->endpoints, k) {
				struct endpoint *e = &p->endpoints.d[k];
				struct host *h = &p->hosts.d[e->host];
				if (e->repo != j->pkg->repo || j->attempts[k] >= p->retries ||
				    h->failures >= p->retries)
					continue;
				possible = true;
				if (now >= h->retry_at)
					priority = MIN(priority, e->mirror.priority);
			}
			vec_foreach(p->endpoints, k) {
				struct endpoint *e = &p->endpoints.d[k];
				struct host *h = &p->hosts.d[e->host];
				if (e->repo != j->pkg->repo || e->mirror.priority != priority ||
				    j->attempts[k] >= p->retries || h->failures >= p->retries ||
				    now < h->retry_at || h->active >= h->limit ||
				    (h->failures && h->active))
					continue;
				/* SRV selection uses weighted reservoir sampling among free targets. */
				if (e->repo->mirror_type == SRV) {
					unsigned int w = e->mirror.weight;
					if (selected == SIZE_MAX || (w && arc4random_uniform(weight + w) < w) ||
					    (!w && !weight && arc4random_uniform(++zero_weights) == 0))
						selected = k;
					if (selected == k && !w && zero_weights == 0)
						zero_weights = 1;
					weight += w;
				} else if (selected == SIZE_MAX)
					selected = k;
			}
			if (selected == SIZE_MAX)
				continue;
			j->endpoint = selected;
			struct endpoint *e = &p->endpoints.d[selected];
			struct url *base = fetchParseURL(e->mirror.url);
			struct url *original = fetchParseURL(j->pkg->repo->url +
			    (strncmp(j->pkg->repo->url, "pkg+", 4) == 0 ? 4 : 0));
			if (base == NULL || original == NULL) {
				fetchFreeURL(base);
				fetchFreeURL(original);
				return (-1);
			}
			char *doc;
			xasprintf(&doc, "%s%s%s%s", base->doc,
			    base->doc[strlen(base->doc) - 1] == '/' ? "" : "/",
			    e->mirror.fullpath ? original->doc + (original->doc[0] == '/') : "",
			    e->mirror.fullpath && original->doc[strlen(original->doc) - 1] != '/' ? "/" : "");
			sb_t path = sb_init();
			sb_printf(&path, "%s%s", doc, j->pkg->repopath);
			free(doc);
			free(base->doc);
			base->doc = sb_get(&path);
			free(j->target);
			j->target = pkg_fetch_url(base);
			fetchFreeURL(base);
			fetchFreeURL(original);
			j->host = e->host;
			j->attempts[selected]++;
		}
		j->generation = p->hosts.d[j->host].generation;
		p->hosts.d[j->host].active++;
		j->state = JOB_ACTIVE;
		vec_push(&p->active, i);
		p->cursor = (i + 1) % p->count;
		/* Rotate to another repository before taking its next queued package. */
		for (size_t k = 1; k < p->count; k++) {
			size_t next = (i + k) % p->count;
			if (p->jobs[next].state == JOB_PENDING && p->jobs[next].pkg->repo != j->pkg->repo) {
				p->cursor = next;
				break;
			}
		}
		p->waiting_since = 0;
		*number = i;
		*target = j->redirect ? j->redirect : j->target;
		return (1);
	}
	if (!active && (!possible || (p->waiting_since && now - p->waiting_since > 300))) {
		for (size_t i = 0; i < p->count; i++)
			if (p->jobs[i].state == JOB_PENDING && p->jobs[i].error[0]) {
				pkg_emit_error("%s", p->jobs[i].error);
				break;
			}
		pkg_emit_error("No usable download hosts remain (retry budget exhausted)");
		return (-1);
	}
	if (!active && p->waiting_since == 0)
		p->waiting_since = now;
	return (0);
}

int
pkg_fetch_plan_result(struct pkg_fetch_plan *p, size_t number, int result,
    int error, const struct fetch_response *response, const char *message)
{
	struct job *j = &p->jobs[number];
	struct host *h = &p->hosts.d[j->host];
	double now = monotime();

	strlcpy(j->error, message ? message : "", sizeof(j->error));
	h->active--;
	vec_foreach(p->active, i) {
		if (p->active.d[i] == number) {
			vec_remove(&p->active, i);
			break;
		}
	}
	j->state = JOB_PENDING;
	if (result == EPKG_OK) {
		j->state = JOB_COMPLETE;
		if (j->generation == h->generation)
			h->failures = 0;
		return (EPKG_END);
	}
	if (result == EPKG_CANCEL)
		return (result);
	if (response->redirect[0] && ++j->redirects <= 10) {
		struct url *u = fetchParseURL(response->redirect);
		if (u == NULL || (strcmp(u->scheme, "http") && strcmp(u->scheme, "https"))) {
			fetchFreeURL(u);
			return (EPKG_FATAL);
		}
		/* Preserve URL credentials only for redirects within the same host. */
		struct url *old = fetchParseURL(j->redirect ? j->redirect : j->target);
		if (old && strcasecmp(u->host, old->host) == 0) {
			strlcpy(u->user, old->user, sizeof(u->user));
			strlcpy(u->pwd, old->pwd, sizeof(u->pwd));
		}
		fetchFreeURL(old);
		free(j->redirect);
		j->redirect = pkg_fetch_url(u);
		fetchFreeURL(u);
		return (host_add(p, j->redirect, &j->host));
	}
	if (response->status == 429 || response->status >= 500 ||
	    error == FETCH_NETWORK || error == FETCH_RESOLV || error == FETCH_DOWN ||
	    error == FETCH_TIMEOUT || error == FETCH_TEMP) {
		h->failures++;
		h->generation++;
		double delay = (1U << MIN(h->failures - 1, 5)) + arc4random_uniform(1000) / 1000.0;
		h->retry_at = MAX(h->retry_at, now + MAX(delay, response->retry_after));
		pkg_dbg(PKG_DBG_FETCH, 1, "suspending host %s for %.1f seconds (%u failures)",
		    h->name, h->retry_at - now, h->failures);
	} else
		j->attempts[j->endpoint] = p->retries;
	free(j->redirect);
	j->redirect = NULL;
	return (EPKG_OK);
}
