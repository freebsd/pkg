/*-
 * Copyright (c) 2012-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pkg_config.h>

#include <sys/stat.h> /* for private.utils.h */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#ifdef HAVE_LDNS
#include <ldns/ldns.h>
#else
#define BIND_8_COMPAT
#include <arpa/nameser.h>
#include <resolv.h>
#endif
#include <netdb.h>

#ifndef NS_QFIXEDSZ
#define NS_QFIXEDSZ     4       /*%< #/bytes of fixed data in query */
#endif

#ifndef NS_INT16SZ
#define NS_INT16SZ      2       /*%< #/bytes of data in a u_int16_t */
#endif

#ifndef NS_INT32SZ
#define NS_INT32SZ      4       /*%< #/bytes of data in a u_int32_t */
#endif

/*%
 * Inline versions of get/put short/long.  Pointer is advanced.
 */
#ifndef NS_GET16
#define NS_GET16(s, cp) do { \
        register const u_char *t_cp = (const u_char *)(cp); \
        (s) = ((u_int16_t)t_cp[0] << 8) \
            | ((u_int16_t)t_cp[1]) \
            ; \
        (cp) += NS_INT16SZ; \
} while (0)
#endif

#ifndef NS_GET32
#define NS_GET32(l, cp) do { \
        register const u_char *t_cp = (const u_char *)(cp); \
        (l) = ((u_int32_t)t_cp[0] << 24) \
            | ((u_int32_t)t_cp[1] << 16) \
            | ((u_int32_t)t_cp[2] << 8) \
            | ((u_int32_t)t_cp[3]) \
            ; \
        (cp) += NS_INT32SZ; \
} while (0)
#endif

#include <bsd_compat.h>
#include "private/utils.h"
#include "xmalloc.h"
#include "pkg.h"

#ifndef HAVE_LDNS
typedef union {
	HEADER hdr;
	unsigned char buf[1024];
} query_t;
#endif

static int
srv_priority_cmp(const void *a, const void *b)
{
	const struct dns_srvinfo *da, *db;
#ifdef HAVE_LDNS
	da = (const struct dns_srvinfo *)a;
	db = (const struct dns_srvinfo *)b;
#else
	da = *(struct dns_srvinfo * const *)a;
	db = *(struct dns_srvinfo * const *)b;
#endif

	return ((da->priority > db->priority) - (da->priority < db->priority));
}

static int
srv_final_cmp(const void *a, const void *b)
{
	const struct dns_srvinfo *da, *db;
	int res;
#ifdef HAVE_LDNS
	da = (const struct dns_srvinfo *)a;
	db = (const struct dns_srvinfo *)b;
#else
	da = *(struct dns_srvinfo * const *)a;
	db = *(struct dns_srvinfo * const *)b;
#endif

	res = ((da->priority > db->priority) - (da->priority < db->priority));
	if (res == 0)
		res = ((db->finalweight > da->finalweight) - (db->finalweight < da->finalweight));

	return (res);
}

#ifndef HAVE_LDNS
static bool
dns_advance(const unsigned char **p, const unsigned char *end, size_t len)
{
	if ((size_t)(end - *p) < len)
		return (false);
	*p += len;
	return (true);
}

static bool
dns_get16(const unsigned char **p, const unsigned char *end,
    unsigned int *value)
{
	if (!dns_advance(p, end, NS_INT16SZ))
		return (false);
	*value = ((unsigned int)(*p)[-NS_INT16SZ] << 8) |
	    (*p)[-1];
	return (true);
}

static bool
dns_get32(const unsigned char **p, const unsigned char *end,
    unsigned int *value)
{
	if (!dns_advance(p, end, NS_INT32SZ))
		return (false);
	*value = ((unsigned int)(*p)[-NS_INT32SZ] << 24) |
	    ((unsigned int)(*p)[-3] << 16) |
	    ((unsigned int)(*p)[-2] << 8) |
	    (*p)[-1];
	return (true);
}

static void
compute_weight(struct dns_srvinfo **d, int first, int last)
{
	int i, j;

	/* Select one record at a time from the remaining priority group. */
	for (i = first; i <= last; i++) {
		uint32_t totalweight = 0;
		uint32_t selected;
		struct dns_srvinfo *tmp;

		for (j = i; j <= last; j++)
			totalweight += d[j]->weight;
		if (totalweight == 0) {
			j = i + random() % (last - i + 1);
		} else {
			selected = random() % totalweight;
			for (j = i; j <= last; j++) {
				if (selected < d[j]->weight)
					break;
				selected -= d[j]->weight;
			}
		}
		tmp = d[i];
		d[i] = d[j];
		d[j] = tmp;
		d[i]->finalweight = last - i + 1;
	}
}

struct dns_srvinfo *
dns_getsrvinfo(const char *zone)
{
	char host[MAXHOSTNAMELEN];
	query_t q;
	int len, qdcount, ancount, n, i;
	struct dns_srvinfo **res, *first;
	const unsigned char *end, *p, *rdata_end;
	unsigned int type, class, ttl, priority, weight, port, rdlen;
	int f, l;

	if ((len = res_query(zone, C_IN, T_SRV, q.buf, sizeof(q.buf))) == -1 ||
	    len < (int)sizeof(HEADER) || len > (int)sizeof(q.buf))
		return (NULL);

	qdcount = ntohs(q.hdr.qdcount);
	ancount = ntohs(q.hdr.ancount);

	end = q.buf + len;
	p = q.buf + sizeof(HEADER);

	while (qdcount > 0) {
		qdcount--;
		if (p >= end ||
		    (len = dn_expand(q.buf, end, p, host, sizeof(host))) < 0 ||
		    !dns_advance(&p, end, len) ||
		    !dns_advance(&p, end, NS_QFIXEDSZ))
			return (NULL);
	}

	if (ancount == 0 || (size_t)(end - p) / 11 < (size_t)ancount)
		return (NULL);
	res = xcalloc(ancount, sizeof(struct dns_srvinfo *));
	n = 0;
	while (ancount > 0) {
		ancount--;
		if (p >= end ||
		    (len = dn_expand(q.buf, end, p, host, sizeof(host))) < 0 ||
		    !dns_advance(&p, end, len) ||
		    !dns_get16(&p, end, &type) ||
		    !dns_get16(&p, end, &class) ||
		    !dns_get32(&p, end, &ttl) ||
		    !dns_get16(&p, end, &rdlen) ||
		    !dns_advance(&p, end, rdlen))
			goto bad_answer;
		rdata_end = p;
		p -= rdlen;

		if (type != T_SRV) {
			p = rdata_end;
			continue;
		}

		if (rdlen < 7 ||
		    !dns_get16(&p, rdata_end, &priority) ||
		    !dns_get16(&p, rdata_end, &weight) ||
		    !dns_get16(&p, rdata_end, &port) ||
		    (len = dn_expand(q.buf, end, p, host, sizeof(host))) < 0 ||
		    !dns_advance(&p, rdata_end, len) || p != rdata_end)
			goto bad_answer;

		res[n] = xmalloc(sizeof(struct dns_srvinfo));
		res[n]->type = type;
		res[n]->class = class;
		res[n]->ttl = ttl;
		res[n]->priority = priority;
		res[n]->weight = weight;
		res[n]->port = port;
		res[n]->next = NULL;
		res[n]->finalweight = 0;
		strlcpy(res[n]->host, host, sizeof(res[n]->host));

		n++;
	}
	if (n == 0)
		goto bad_answer;

	/* order by priority */
	qsort(res, n, sizeof(res[0]), srv_priority_cmp);

	f = 0;
	l = 0;
	for (i = 1; i < n; i++) {
		if (res[i]->priority != res[f]->priority) {
			if (f != l)
				compute_weight(res, f, l);
			f = i;
		}
		l = i;
	}
	if (f != l)
		compute_weight(res, f, l);

	qsort(res, n, sizeof(res[0]), srv_final_cmp);

	for (i = 0; i < n - 1; i++)
		res[i]->next = res[i + 1];

	/* Sort against priority then weight */

	first = res[0];
	free(res);

	return (first);

bad_answer:
	for (i = 0; i < n; i++)
		free(res[i]);
	free(res);
	return (NULL);
}

int
set_nameserver(const char *nsname) {
#ifndef HAVE___RES_SETSERVERS
	return (-1);
#else
	struct __res_state res;
	union res_sockaddr_union u[MAXNS];
	struct addrinfo *answer = NULL;
	struct addrinfo *cur = NULL;
	struct addrinfo hint;
	int nscount = 0;

	memset(u, 0, sizeof(u));
	memset(&hint, 0, sizeof(hint));
	memset(&res, 0, sizeof(res));
	hint.ai_socktype = SOCK_DGRAM;
	hint.ai_flags = AI_NUMERICHOST;

	if (res_ninit(&res) == -1)
		return (-1);

	if (getaddrinfo(nsname, NULL, &hint, &answer) == 0) {
		for (cur = answer; cur != NULL; cur = cur->ai_next) {
			if (nscount == MAXNS)
				break;
			switch (cur->ai_addr->sa_family) {
			case AF_INET6:
				u[nscount].sin6 = *(struct sockaddr_in6*)(void *)cur->ai_addr;
				u[nscount++].sin6.sin6_port = htons(53);
				break;
			case AF_INET:
				u[nscount].sin = *(struct sockaddr_in*)(void *)cur->ai_addr;
				u[nscount++].sin.sin_port = htons(53);
				break;
			}
		}
		if (nscount != 0)
			res_setservers(&res, u, nscount);
		freeaddrinfo(answer);
	}
	if (nscount == 0)
		return (-1);

	_res = res;

	return (0);
#endif
}
#else

static ldns_resolver *lres = NULL;

static void
compute_weight(struct dns_srvinfo *d, int first, int last)
{
	int i, j;

	/* Select one record at a time from the remaining priority group. */
	for (i = first; i <= last; i++) {
		uint32_t totalweight = 0;
		uint32_t selected;
		struct dns_srvinfo tmp;

		for (j = i; j <= last; j++)
			totalweight += d[j].weight;
		if (totalweight == 0) {
			j = i + random() % (last - i + 1);
		} else {
			selected = random() % totalweight;
			for (j = i; j <= last; j++) {
				if (selected < d[j].weight)
					break;
				selected -= d[j].weight;
			}
		}
		tmp = d[i];
		d[i] = d[j];
		d[j] = tmp;
		d[i].finalweight = last - i + 1;
	}
}

struct dns_srvinfo *
dns_getsrvinfo(const char *zone)
{
	ldns_rdf *domain;
	ldns_pkt *p;
	ldns_rr_list *srv;
	struct dns_srvinfo *res;
	int ancount, i;
	int f, l;

	if (lres == NULL)
		if (ldns_resolver_new_frm_file(&lres, NULL) != LDNS_STATUS_OK)
			return (NULL);

	domain = ldns_dname_new_frm_str(zone);
	if (domain == NULL)
		return (NULL);

	p = ldns_resolver_query(lres, domain,
	    LDNS_RR_TYPE_SRV,
	    LDNS_RR_CLASS_IN,
	    LDNS_RD);

	ldns_rdf_deep_free(domain);

	if (p == NULL)
		return (NULL);

	srv = ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_SRV, LDNS_SECTION_ANSWER);
	ldns_pkt_free(p);

	if (srv == NULL)
		return (NULL);

	ancount = ldns_rr_list_rr_count(srv);
	res = xcalloc(ancount, sizeof(struct dns_srvinfo));

	for (i = 0; i < ancount; i ++) {
		ldns_rr *rr;

		rr = ldns_rr_list_rr(srv, i);
		if (rr != NULL) {
			char *host;
			res[i].class = ldns_rr_get_class(rr);
			res[i].ttl = ldns_rr_ttl(rr);
			res[i].priority = ldns_rdf2native_int16(ldns_rr_rdf(rr, 0));
			res[i].weight = ldns_rdf2native_int16(ldns_rr_rdf(rr, 1));
			res[i].port = ldns_rdf2native_int16(ldns_rr_rdf(rr, 2));
			host = ldns_rdf2str(ldns_rr_rdf(rr, 3));
			strlcpy(res[i].host, host, sizeof(res[i].host));
			free(host);
		}
	}

	ldns_rr_list_deep_free(srv);

	/* order by priority */
	qsort(res, ancount, sizeof(res[0]), srv_priority_cmp);

	f = 0;
	l = 0;
	for (i = 1; i < ancount; i++) {
		if (res[i].priority != res[f].priority) {
			if (f != l)
				compute_weight(res, f, l);
			f = i;
		}
		l = i;
	}
	if (f != l)
		compute_weight(res, f, l);

	/* Sort against priority then weight */
	qsort(res, ancount, sizeof(res[0]), srv_final_cmp);

	for (i = 0; i < ancount - 1; i++)
		res[i].next = &res[i + 1];

	return (res);
}

int
set_nameserver(const char *nsname)
{
	/*
	 * XXX: can we use the system resolver to resolve this name ??
	 * The current code does this, but it is unlikely a good solution
	 * So here we allow IP addresses only
	 */
	ldns_rdf *rdf;

	rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_A, nsname);
	if (rdf == NULL)
		rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_AAAA, nsname);

	if (rdf == NULL)
		return (EPKG_FATAL);

	if (lres == NULL)
		if (ldns_resolver_new_frm_file(&lres, NULL) != LDNS_STATUS_OK)
			return (EPKG_FATAL);

	if (ldns_resolver_push_nameserver(lres, rdf) != LDNS_STATUS_OK)
		return (EPKG_FATAL);

	return (EPKG_OK);
}
#endif
