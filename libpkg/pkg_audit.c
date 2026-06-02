/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2020 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/mman.h>

#include <archive.h>
#include <err.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <string.h>
#include <xstring.h>

#include <yxml.h>

#ifdef __linux__
# ifdef __GLIBC__
#  include <sys/time.h>
# endif
#endif

#include "pkg.h"
#include "pkg/audit.h"
#include "private/pkg.h"
#include "private/event.h"

/*
 * The _sorted stuff.
 *
 * We are using the optimized search based on the following observations:
 *
 * - number of VuXML entries is more likely to be far greater than
 *   the number of installed ports; thus we should try to optimize
 *   the walk through all entries for a given port;
 *
 * - fnmatch() is good and fast, but if we will compare the audit entry
 *   name prefix without globbing characters to the prefix of port name
 *   of the same length and they are different, there is no point to
 *   check the rest;
 *
 * - (most important bit): if parsed VuXML entries are lexicographically
 *   sorted per the largest prefix with no globbing characters and we
 *   know how many succeeding entries have the same prefix we can
 *
 *   a. skip the rest of the entries once the non-globbing prefix is
 *      lexicographically larger than the port name prefix of the
 *      same length: all successive prefixes will be larger as well;
 *
 *   b. if we have non-globbing prefix that is lexicographically smaller
 *      than port name prefix, we can skip all succeeding entries with
 *      the same prefix; and as some port names tend to repeat due to
 *      multiple vulnerabilities, it could be a large win.
 */
struct pkg_audit_item {
	struct pkg_audit_entry *e;	/* Entry itself */
	size_t pkg_idx;			/* Index into e->packages */
	size_t name_idx;		/* Index into e->packages.d[pkg_idx].names */
	size_t noglob_len;		/* Prefix without glob characters */
	size_t next_pfx_incr;		/* Index increment for the entry with
					   different prefix */
};

struct pkg_audit {
	audit_entryv_t entries;
	struct pkg_audit_item *items;
	bool parsed;
	bool loaded;
	char **ignore_globs;
	char **ignore_regexp;
	void *map;
	size_t len;
};


/*
 * Another small optimization to skip the beginning of the
 * VuXML entry array, if possible.
 *
 * audit_entry_first_byte_idx[ch] represents the index
 * of the first VuXML entry in the sorted array that has
 * its non-globbing prefix that is started with the character
 * 'ch'.  It allows to skip entries from the beginning of the
 * VuXML array that aren't relevant for the checked port name.
 */
static size_t audit_entry_first_byte_idx[256];

static void
pkg_audit_free_entry(struct pkg_audit_entry *e)
{
	vec_foreach(e->packages, pi) {
		struct pkg_audit_package *p = &e->packages.d[pi];
		vec_foreach(p->names, ni)
			free(p->names.d[ni].pkgname);
		vec_free(&p->names);
		vec_foreach(p->versions, vi) {
			free(p->versions.d[vi].v1.version);
			free(p->versions.d[vi].v2.version);
		}
		vec_free(&p->versions);
	}
	vec_free(&e->packages);
	vec_foreach(e->cve, ci)
		free(e->cve.d[ci].cvename);
	vec_free(&e->cve);
	free(e->url);
	free(e->desc);
	free(e->id);
}

static void
pkg_audit_free_entries(audit_entryv_t *entries)
{
	vec_foreach(*entries, i)
		pkg_audit_free_entry(&entries->d[i]);
	vec_free(entries);
}

struct pkg_audit_extract_cbdata {
	int out;
	const char *fname;
	const char *dest;
};

static int
pkg_audit_sandboxed_extract(int fd, void *ud)
{
	struct pkg_audit_extract_cbdata *cbdata = ud;
	int rc = EPKG_OK;
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;

	a = archive_read_new();
#if ARCHIVE_VERSION_NUMBER < 3000002
	archive_read_support_compression_all(a);
#else
	archive_read_support_filter_all(a);
#endif

	archive_read_support_format_raw(a);

	if (archive_read_open_fd(a, fd, 4096) != ARCHIVE_OK) {
		pkg_emit_error("archive_read_open_filename(%s) failed: %s",
				cbdata->fname, archive_error_string(a));
		rc = EPKG_FATAL;
	}
	else {
		while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
			if (archive_read_data_into_fd(a, cbdata->out) != ARCHIVE_OK) {
				pkg_emit_error("archive_read_data_into_fd(%s) failed: %s",
						cbdata->dest, archive_error_string(a));
				break;
			}
		}
		archive_read_close(a);
		archive_read_free(a);
	}

	return (rc);
}

int
pkg_audit_fetch(const char *src, const char *dest)
{
	int fd = -1, outfd = -1;
	char tmp[MAXPATHLEN];
	const char *tmpdir;
	int retcode = EPKG_FATAL;
	time_t t = 0;
	struct stat st;
	struct pkg_audit_extract_cbdata cbdata;
	int dfd = -1;
	struct timespec ts[2] = {
		{
		.tv_nsec = 0
		},
		{
		.tv_nsec = 0
		}
	};

	if (src == NULL) {
		src = pkg_object_string(pkg_config_get("VULNXML_SITE"));
	}

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";

	strlcpy(tmp, tmpdir, sizeof(tmp));
	strlcat(tmp, "/vuln.xml.XXXXXXXXXX", sizeof(tmp));

	if (dest != NULL) {
		if (stat(dest, &st) != -1)
			t = st.st_mtime;
	} else {
		dfd = pkg_get_dbdirfd();
		if (fstatat(dfd, "vuln.xml", &st, 0) != -1)
			t = st.st_mtime;
	}

	switch (pkg_fetch_file_tmp(NULL, src, tmp, t, &fd)) {
	case EPKG_OK:
		break;
	case EPKG_UPTODATE:
		pkg_emit_notice("vulnxml file up-to-date");
		retcode = EPKG_OK;
		goto cleanup;
	default:
		pkg_emit_error("cannot fetch vulnxml file");
		goto cleanup;
	}
	/* Open out fd */
	if (dest != NULL) {
		outfd = open(dest, O_RDWR|O_CREAT|O_TRUNC,
		    S_IRUSR|S_IRGRP|S_IROTH);
	} else {
		outfd = openat(dfd, "vuln.xml", O_RDWR|O_CREAT|O_TRUNC,
		    S_IRUSR|S_IRGRP|S_IROTH);
	}
	if (outfd == -1) {
		pkg_emit_errno("pkg_audit_fetch", "open out fd");
		goto cleanup;
	}

	cbdata.fname = tmp;
	cbdata.out = outfd;
	cbdata.dest = dest;
	fstat(fd, &st);

	/* Call sandboxed */
	retcode = pkg_emit_sandbox_call(pkg_audit_sandboxed_extract, fd, &cbdata);
	ts[0].tv_sec = st.st_mtime;
	ts[1].tv_sec = st.st_mtime;
	futimens(outfd, ts);

cleanup:
	if (fd != -1)
		close(fd);
	if (outfd != -1)
		close(outfd);

	return (retcode);
}

enum vulnxml_parse_state {
	VULNXML_PARSE_INIT = 0,
	VULNXML_PARSE_VULN,
	VULNXML_PARSE_TOPIC,
	VULNXML_PARSE_PACKAGE,
	VULNXML_PARSE_PACKAGE_NAME,
	VULNXML_PARSE_RANGE,
	VULNXML_PARSE_RANGE_GT,
	VULNXML_PARSE_RANGE_GE,
	VULNXML_PARSE_RANGE_LT,
	VULNXML_PARSE_RANGE_LE,
	VULNXML_PARSE_RANGE_EQ,
	VULNXML_PARSE_CVE
};

enum vulnxml_parse_attribute_state {
	VULNXML_ATTR_NONE = 0,
	VULNXML_ATTR_VID,
};

struct vulnxml_userdata {
	struct pkg_audit_entry *cur_entry;
	struct pkg_audit *audit;
	enum vulnxml_parse_state state;
	xstring *content;
	int range_num;
	enum vulnxml_parse_attribute_state attr;
};

static void
vulnxml_start_element(struct vulnxml_userdata *ud, yxml_t *xml)
{
	if (ud->state == VULNXML_PARSE_INIT && STRIEQ(xml->elem, "vuln")) {
		vec_push(&ud->audit->entries, ((struct pkg_audit_entry){0}));
		ud->cur_entry = &ud->audit->entries.d[ud->audit->entries.len - 1];
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_VULN && STRIEQ(xml->elem, "topic")) {
		ud->state = VULNXML_PARSE_TOPIC;
	}
	else if (ud->state == VULNXML_PARSE_VULN && STRIEQ(xml->elem, "package")) {
		vec_push(&ud->cur_entry->packages, ((struct pkg_audit_package){0}));
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_VULN && STRIEQ(xml->elem, "cvename")) {
		ud->state = VULNXML_PARSE_CVE;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && STRIEQ(xml->elem, "name")) {
		ud->state = VULNXML_PARSE_PACKAGE_NAME;
		struct pkg_audit_package *cur_pkg =
		    &ud->cur_entry->packages.d[ud->cur_entry->packages.len - 1];
		vec_push(&cur_pkg->names, ((struct pkg_audit_pkgname){0}));
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && STRIEQ(xml->elem, "range")) {
		ud->state = VULNXML_PARSE_RANGE;
		struct pkg_audit_package *cur_pkg =
		    &ud->cur_entry->packages.d[ud->cur_entry->packages.len - 1];
		vec_push(&cur_pkg->versions, ((struct pkg_audit_versions_range){0}));
		ud->range_num = 0;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && STRIEQ(xml->elem, "gt")) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_GT;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && STRIEQ(xml->elem, "ge")) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_GE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && STRIEQ(xml->elem, "lt")) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_LT;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && STRIEQ(xml->elem, "le")) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_LE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && STRIEQ(xml->elem, "eq")) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_EQ;
	}
}

static void
vulnxml_end_element(struct vulnxml_userdata *ud, yxml_t *xml)
{
	struct pkg_audit_versions_range *vers;
	int range_type = -1;

	fflush(ud->content->fp);
	if (ud->state == VULNXML_PARSE_VULN && STRIEQ(xml->elem, "vuxml")) {
		/* Entry is already in the vec, just validate it */
		struct pkg_audit_entry *e = ud->cur_entry;
		if (e->packages.len == 0 ||
		    (e->packages.len > 0 && e->packages.d[0].names.len == 0)) {
			/* Invalid entry: remove from vec and free */
			pkg_audit_free_entry(e);
			ud->audit->entries.len--;
		}
		ud->state = VULNXML_PARSE_INIT;
	}
	else if (ud->state == VULNXML_PARSE_TOPIC && STRIEQ(xml->elem, "vuln")) {
		ud->cur_entry->desc = xstrdup(ud->content->buf);
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_CVE && STRIEQ(xml->elem, "references")) {
		vec_push(&ud->cur_entry->cve,
		    ((struct pkg_audit_cve){ .cvename = xstrdup(ud->content->buf) }));
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && STRIEQ(xml->elem, "affects")) {
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE_NAME && STRIEQ(xml->elem, "package")) {
		struct pkg_audit_package *cur_pkg =
		    &ud->cur_entry->packages.d[ud->cur_entry->packages.len - 1];
		struct pkg_audit_pkgname *cur_name =
		    &cur_pkg->names.d[cur_pkg->names.len - 1];
		cur_name->pkgname = xstrdup(ud->content->buf);
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && STRIEQ(xml->elem, "package")) {
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_GT && STRIEQ(xml->elem, "range")) {
		range_type = GT;
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_GE && STRIEQ(xml->elem, "range")) {
		range_type = GTE;
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_LT && STRIEQ(xml->elem, "range")) {
		range_type = LT;
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_LE && STRIEQ(xml->elem, "range")) {
		range_type = LTE;
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_EQ && STRIEQ(xml->elem, "range")) {
		range_type = EQ;
		ud->state = VULNXML_PARSE_RANGE;
	}

	if (range_type > 0) {
		struct pkg_audit_package *cur_pkg =
		    &ud->cur_entry->packages.d[ud->cur_entry->packages.len - 1];
		vers = &cur_pkg->versions.d[cur_pkg->versions.len - 1];
		if (ud->range_num == 1) {
			vers->v1.version = xstrdup(ud->content->buf);
			vers->v1.type = range_type;
		}
		else if (ud->range_num == 2) {
			vers->v2.version = xstrdup(ud->content->buf);
			vers->v2.type = range_type;
		}
	}
	xstring_reset(ud->content);
}

static void
vulnxml_start_attribute(struct vulnxml_userdata *ud, yxml_t *xml)
{
	if (ud->state != VULNXML_PARSE_VULN)
		return;

	if (STRIEQ(xml->attr, "vid"))
		ud->attr = VULNXML_ATTR_VID;
}

static void
vulnxml_end_attribute(struct vulnxml_userdata *ud, yxml_t *xml __unused)
{
	fflush(ud->content->fp);
	if (ud->state == VULNXML_PARSE_VULN && ud->attr == VULNXML_ATTR_VID) {
		ud->cur_entry->id = xstrdup(ud->content->buf);
		ud->attr = VULNXML_ATTR_NONE;
	}
	xstring_reset(ud->content);
}

static void
vulnxml_val_attribute(struct vulnxml_userdata *ud, yxml_t *xml)
{
	if (ud->state == VULNXML_PARSE_VULN && ud->attr == VULNXML_ATTR_VID) {
		fputs(xml->data, ud->content->fp);
	}
}

static void
vulnxml_handle_data(struct vulnxml_userdata *ud, yxml_t *xml)
{

	switch(ud->state) {
	case VULNXML_PARSE_INIT:
	case VULNXML_PARSE_VULN:
	case VULNXML_PARSE_PACKAGE:
	case VULNXML_PARSE_RANGE:
		/* On these states we do not need any data */
		break;
	case VULNXML_PARSE_TOPIC:
	case VULNXML_PARSE_PACKAGE_NAME:
	case VULNXML_PARSE_CVE:
	case VULNXML_PARSE_RANGE_GT:
	case VULNXML_PARSE_RANGE_GE:
	case VULNXML_PARSE_RANGE_LT:
	case VULNXML_PARSE_RANGE_LE:
	case VULNXML_PARSE_RANGE_EQ:
		fputs(xml->data, ud->content->fp);
		break;
	}
}

static int
pkg_audit_parse_vulnxml(struct pkg_audit *audit)
{
	int ret = EPKG_FATAL;
	yxml_t x;
	yxml_ret_t r;
	char buf[BUFSIZ];
	char *walk, *end;
	struct vulnxml_userdata ud;

	yxml_init(&x, buf, BUFSIZ);
	ud.cur_entry = NULL;
	ud.audit = audit;
	ud.range_num = 0;
	ud.state = VULNXML_PARSE_INIT;
	ud.content = xstring_new();

	walk = audit->map;
	end = walk + audit->len;
	while (walk < end) {
		r = yxml_parse(&x, *walk++);
		switch (r) {
		case YXML_EEOF:
		case YXML_EREF:
		case YXML_ESTACK:
			pkg_emit_error("Unexpected EOF while parsing vulnxml");
			goto out;
		case YXML_ESYN:
			pkg_emit_error("Syntax error while parsing vulnxml");
			goto out;
		case YXML_ECLOSE:
			pkg_emit_error("Close tag does not match open tag line %d", x.line);
			goto out;
		case YXML_ELEMSTART:
			vulnxml_start_element(&ud, &x);
				break;
		case YXML_ELEMEND:
			vulnxml_end_element(&ud, &x);
			break;
		case YXML_CONTENT:
			vulnxml_handle_data(&ud, &x);
			break;
		case YXML_ATTRVAL:
			vulnxml_val_attribute(&ud, &x);
			break;
		case YXML_ATTRSTART:
			vulnxml_start_attribute(&ud, &x);
			break;
			/* ignore */
		case YXML_ATTREND:
			vulnxml_end_attribute(&ud, &x);
			/* ignore */
			break;
		case YXML_OK:
		case YXML_PISTART:
		case YXML_PICONTENT:
		case YXML_PIEND:
			break;
		}
	}

	if (yxml_eof(&x) == YXML_OK)
		ret = EPKG_OK;
	else
		pkg_emit_error("Invalid end of XML");
out:
	xstring_free(ud.content);

	return (ret);
}

/*
 * Returns the length of the largest prefix without globbing
 * characters, as per fnmatch().
 */
static size_t
pkg_audit_str_noglob_len(const char *s)
{
	size_t n;

	for (n = 0; s[n] && s[n] != '*' && s[n] != '?' &&
	    s[n] != '[' && s[n] != '{' && s[n] != '\\'; n++);

	return (n);
}

/*
 * Helper to get pkgname from an audit item.
 */
static const char *
pkg_audit_item_pkgname(const struct pkg_audit_item *item)
{
	return item->e->packages.d[item->pkg_idx].names.d[item->name_idx].pkgname;
}

/*
 * Helper for quicksort that lexicographically orders prefixes.
 */
static int
pkg_audit_entry_cmp(const void *a, const void *b)
{
	const struct pkg_audit_item *e1, *e2;
	size_t min_len;
	int result;

	e1 = (const struct pkg_audit_item *)a;
	e2 = (const struct pkg_audit_item *)b;

	min_len = (e1->noglob_len < e2->noglob_len ?
	    e1->noglob_len : e2->noglob_len);
	result = strncmp(pkg_audit_item_pkgname(e1),
	    pkg_audit_item_pkgname(e2), min_len);
	/*
	 * Additional check to see if some word is a prefix of an
	 * another one and, thus, should go before the former.
	 */
	if (result == 0) {
		if (e1->noglob_len < e2->noglob_len)
			result = -1;
		else if (e1->noglob_len > e2->noglob_len)
			result = 1;
	}

	return (result);
}

/*
 * Sorts VuXML entries and calculates increments to jump to the
 * next distinct prefix.
 */
static struct pkg_audit_item *
pkg_audit_preprocess(audit_entryv_t *entries)
{
	struct pkg_audit_item *ret;
	size_t i, n, tofill;

	/* Count total items (entry x package x name) */
	n = 0;
	vec_foreach(*entries, ei) {
		struct pkg_audit_entry *e = &entries->d[ei];
		vec_foreach(e->packages, pi) {
			n += e->packages.d[pi].names.len;
		}
	}

	ret = xcalloc(n + 1, sizeof(ret[0]));
	n = 0;

	vec_foreach(*entries, ei) {
		struct pkg_audit_entry *e = &entries->d[ei];
		vec_foreach(e->packages, pi) {
			struct pkg_audit_package *p = &e->packages.d[pi];
			vec_foreach(p->names, ni) {
				const char *pkgname = p->names.d[ni].pkgname;
				if (pkgname != NULL) {
					ret[n].e = e;
					ret[n].pkg_idx = pi;
					ret[n].name_idx = ni;
					ret[n].noglob_len = pkg_audit_str_noglob_len(pkgname);
					ret[n].next_pfx_incr = 1;
					n++;
				}
			}
		}
	}

	qsort(ret, n, sizeof(*ret), pkg_audit_entry_cmp);

	if (n < 2)
		goto first_byte_idx;

	/*
	 * Determining jump indexes to the next different prefix.
	 * Only non-1 increments are calculated there.
	 *
	 * Due to the current usage that picks only increment for the
	 * first of the non-unique prefixes in a row, we could
	 * calculate only that one and skip calculations for the
	 * succeeding, but for the uniformity and clarity we're
	 * calculating 'em all.
	 */
	for (n = 1, tofill = 0; ret[n].e; n++) {
		if (ret[n - 1].noglob_len != ret[n].noglob_len) {
			struct pkg_audit_item *base;

			base = ret + n - tofill;
			for (i = 0; tofill > 1; i++, tofill--)
				base[i].next_pfx_incr = tofill;
			tofill = 1;
		} else if (STREQ(pkg_audit_item_pkgname(&ret[n - 1]),
		    pkg_audit_item_pkgname(&ret[n]))) {
			tofill++;
		} else {
			tofill = 1;
		}
	}

first_byte_idx:
	/* Calculate jump indexes for the first byte of the package name */
	memset(audit_entry_first_byte_idx, '\0', sizeof(audit_entry_first_byte_idx));
	for (n = 1, i = 0; n < 256; n++) {
		while (ret[i].e != NULL &&
		    (size_t)(pkg_audit_item_pkgname(&ret[i])[0]) < n)
			i++;
		audit_entry_first_byte_idx[n] = i;
	}

	return (ret);
}

static bool
pkg_audit_version_match(const char *pkgversion, struct pkg_audit_version *v)
{
	bool res = false;

	/*
	 * Return true so it is easier for the caller to handle case where there is
	 * only one version to match: the missing one will always match.
	 */
	if (v->version == NULL)
		return (true);

	switch (pkg_version_cmp(pkgversion, v->version)) {
	case -1:
		if (v->type == LT || v->type == LTE)
			res = true;
		break;
	case 0:
		if (v->type == EQ || v->type == LTE || v->type == GTE)
			res = true;
		break;
	case 1:
		if (v->type == GT || v->type == GTE)
			res = true;
		break;
	}
	return (res);
}

static void
pkg_audit_add_entry(struct pkg_audit_entry *e, size_t pkg_idx,
    struct pkg_audit_issues **ai)
{
	if (*ai == NULL)
		*ai = xcalloc(1, sizeof(**ai));
	vec_push(&(*ai)->issues,
	    ((struct pkg_audit_issue){ .audit = e, .pkg_idx = pkg_idx }));
}

bool
pkg_audit_is_vulnerable(struct pkg_audit *audit, struct pkg *pkg,
    struct pkg_audit_issues **ai, bool stop_quick)
{
	struct pkg_audit_entry *e;
	struct pkg_audit_item *a;
	bool res = false, res1, res2;

	if (!audit->parsed)
		return false;

	/* check if we decided to ignore that package or not */
	if (match_ucl_lists(pkg->name,
	    pkg_config_get("AUDIT_IGNORE_GLOB"),
	    pkg_config_get("AUDIT_IGNORE_REGEX")))
		return (false);

	a = audit->items;
	a += audit_entry_first_byte_idx[(size_t)pkg->name[0]];

	for (; (e = a->e) != NULL; a += a->next_pfx_incr) {
		int cmp;
		size_t i;
		const char *pkgname;

		/*
		 * Audit entries are sorted, so if we had found one
		 * that is lexicographically greater than our name,
		 * it and the rest won't match our name.
		 */
		pkgname = pkg_audit_item_pkgname(a);
		cmp = strncmp(pkg->name, pkgname, a->noglob_len);
		if (cmp > 0)
			continue;
		else if (cmp < 0)
			break;

		for (i = 0; i < a->next_pfx_incr; i++) {
			e = a[i].e;
			pkgname = pkg_audit_item_pkgname(&a[i]);
			if (fnmatch(pkgname, pkg->name, 0) != 0)
				continue;

			if (pkg->version == NULL) {
				/*
				 * Assume that all versions should be checked
				 */
				res = true;
				pkg_audit_add_entry(e, a[i].pkg_idx, ai);
			}
			else {
				audit_versv_t *versions =
				    &e->packages.d[a[i].pkg_idx].versions;
				vec_foreach(*versions, vi) {
					struct pkg_audit_versions_range *vers =
					    &versions->d[vi];
					res1 = pkg_audit_version_match(
					    pkg->version, &vers->v1);
					res2 = pkg_audit_version_match(
					    pkg->version, &vers->v2);

					if (res1 && res2) {
						res = true;
						pkg_audit_add_entry(e,
						    a[i].pkg_idx, ai);
						break;
					}
				}
			}

			if (res && stop_quick)
				return (res);
		}
	}

	return (res);
}

struct pkg_audit *
pkg_audit_new(void)
{
	struct pkg_audit *audit;

	audit = xcalloc(1, sizeof(struct pkg_audit));

	return (audit);
}

int
pkg_audit_load(struct pkg_audit *audit, const char *fname)
{
	int dfd, fd;
	void *mem;
	struct stat st;

	if (fname != NULL) {
		if ((fd = open(fname, O_RDONLY)) == -1)
			return (EPKG_FATAL);
	} else {
		dfd = pkg_get_dbdirfd();
		if ((fd = openat(dfd, "vuln.xml", O_RDONLY)) == -1)
			return (EPKG_FATAL);
	}

	if (fstat(fd, &st) == -1) {
		close(fd);
		return (EPKG_FATAL);
	}

	if ((mem = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		close(fd);
		return (EPKG_FATAL);
	}
	close(fd);

	audit->map = mem;
	audit->len = st.st_size;
	audit->loaded = true;

	return (EPKG_OK);
}

/* This can and should be executed after cap_enter(3) */
int
pkg_audit_process(struct pkg_audit *audit)
{
	if (geteuid() == 0)
		return (EPKG_FATAL);

	if (!audit->loaded)
		return (EPKG_FATAL);

	if (pkg_audit_parse_vulnxml(audit) == EPKG_FATAL)
		return (EPKG_FATAL);

	audit->items = pkg_audit_preprocess(&audit->entries);
	audit->parsed = true;

	return (EPKG_OK);
}

void
pkg_audit_free (struct pkg_audit *audit)
{
	if (audit != NULL) {
		if (audit->parsed) {
			pkg_audit_free_entries(&audit->entries);
			free(audit->items);
		}
		if (audit->loaded) {
			munmap(audit->map, audit->len);
		}
		free(audit);
	}
}

void
pkg_audit_issues_free(struct pkg_audit_issues *issues)
{
	if (issues == NULL)
		return;

	vec_free(&issues->issues);
	free(issues);
}
