/*-
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
#include <utlist.h>
#include <xstring.h>

#include <yxml.h>

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
	size_t noglob_len;	/* Prefix without glob characters */
	size_t next_pfx_incr;	/* Index increment for the entry with
				   different prefix */
};

struct pkg_audit {
	struct pkg_audit_entry *entries;
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
	struct pkg_audit_package *ppkg, *ppkg_tmp;
	struct pkg_audit_versions_range *vers, *vers_tmp;
	struct pkg_audit_cve *cve, *cve_tmp;
	struct pkg_audit_pkgname *pname, *pname_tmp;

	if (!e->ref) {
		LL_FOREACH_SAFE(e->packages, ppkg, ppkg_tmp) {
			LL_FOREACH_SAFE(ppkg->versions, vers, vers_tmp) {
				free(vers->v1.version);
				free(vers->v2.version);
				free(vers);
			}

			LL_FOREACH_SAFE(ppkg->names, pname, pname_tmp) {
				free(pname->pkgname);
				free(pname);
			}
		}
		LL_FOREACH_SAFE(e->cve, cve, cve_tmp) {
			free(cve->cvename);
			free(cve);
		}
			free(e->url);
			free(e->desc);
			free(e->id);
	}
	free(e);
}

static void
pkg_audit_free_list(struct pkg_audit_entry *h)
{
	struct pkg_audit_entry *e;

	while (h) {
		e = h;
		h = h->next;
		pkg_audit_free_entry(e);
	}
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
	int ret, rc = EPKG_OK;
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
		while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
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

	switch (pkg_fetch_file_tmp(NULL, src, tmp, t)) {
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

	/* Open input fd */
	fd = open(tmp, O_RDONLY);
	if (fd == -1) {
		retcode = EPKG_FATAL;
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

	/* Call sandboxed */
	retcode = pkg_emit_sandbox_call(pkg_audit_sandboxed_extract, fd, &cbdata);

cleanup:
	unlink(tmp);

	if (fd != -1)
		close(fd);
	if (outfd != -1)
		close(outfd);

	return (retcode);
}

/*
 * Expand multiple names to a set of audit entries
 */
static void
pkg_audit_expand_entry(struct pkg_audit_entry *entry, struct pkg_audit_entry **head)
{
	struct pkg_audit_entry *n;
	struct pkg_audit_pkgname *ncur;
	struct pkg_audit_package *pcur;

	/* Set the name of the current entry */
	if (entry->packages == NULL || entry->packages->names == NULL) {
		pkg_audit_free_entry(entry);
		return;
	}

	LL_FOREACH(entry->packages, pcur) {
		LL_FOREACH(pcur->names, ncur) {
			n = xcalloc(1, sizeof(struct pkg_audit_entry));
			n->pkgname = ncur->pkgname;
			/* Set new entry as reference entry */
			n->ref = true;
			n->cve = entry->cve;
			n->desc = entry->desc;
			n->versions = pcur->versions;
			n->url = entry->url;
			n->id = entry->id;
			LL_PREPEND(*head, n);
		}
	}
	LL_PREPEND(*head, entry);
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
	struct pkg_audit_versions_range *vers;
	struct pkg_audit_pkgname *name_entry;
	struct pkg_audit_package *pkg_entry;

	if (ud->state == VULNXML_PARSE_INIT && strcasecmp(xml->elem, "vuln") == 0) {
		ud->cur_entry = xcalloc(1, sizeof(struct pkg_audit_entry));
		ud->cur_entry->next = ud->audit->entries;
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_VULN && strcasecmp(xml->elem, "topic") == 0) {
		ud->state = VULNXML_PARSE_TOPIC;
	}
	else if (ud->state == VULNXML_PARSE_VULN && strcasecmp(xml->elem, "package") == 0) {
		pkg_entry = xcalloc(1, sizeof(struct pkg_audit_package));
		LL_PREPEND(ud->cur_entry->packages, pkg_entry);
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_VULN && strcasecmp(xml->elem, "cvename") == 0) {
		ud->state = VULNXML_PARSE_CVE;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && strcasecmp(xml->elem, "name") == 0) {
		ud->state = VULNXML_PARSE_PACKAGE_NAME;
		name_entry = xcalloc(1, sizeof(struct pkg_audit_pkgname));
		LL_PREPEND(ud->cur_entry->packages->names, name_entry);
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && strcasecmp(xml->elem, "range") == 0) {
		ud->state = VULNXML_PARSE_RANGE;
		vers = xcalloc(1, sizeof(struct pkg_audit_versions_range));
		LL_PREPEND(ud->cur_entry->packages->versions, vers);
		ud->range_num = 0;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(xml->elem, "gt") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_GT;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(xml->elem, "ge") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_GE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(xml->elem, "lt") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_LT;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(xml->elem, "le") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_LE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(xml->elem, "eq") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_EQ;
	}
}

static void
vulnxml_end_element(struct vulnxml_userdata *ud, yxml_t *xml)
{
	struct pkg_audit_cve *cve;
	struct pkg_audit_entry *entry;
	struct pkg_audit_versions_range *vers;
	int range_type = -1;

	fflush(ud->content->fp);
	if (ud->state == VULNXML_PARSE_VULN && strcasecmp(xml->elem, "vuxml") == 0) {
		pkg_audit_expand_entry(ud->cur_entry, &ud->audit->entries);
		ud->state = VULNXML_PARSE_INIT;
	}
	else if (ud->state == VULNXML_PARSE_TOPIC && strcasecmp(xml->elem, "vuln") == 0) {
		ud->cur_entry->desc = xstrdup(ud->content->buf);
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_CVE && strcasecmp(xml->elem, "references") == 0) {
		entry = ud->cur_entry;
		cve = xmalloc(sizeof(struct pkg_audit_cve));
		cve->cvename = xstrdup(ud->content->buf);
		LL_PREPEND(entry->cve, cve);
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && strcasecmp(xml->elem, "affects") == 0) {
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE_NAME && strcasecmp(xml->elem, "package") == 0) {
		ud->cur_entry->packages->names->pkgname = xstrdup(ud->content->buf);
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(xml->elem, "package") == 0) {
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_GT && strcasecmp(xml->elem, "range") == 0) {
		range_type = GT;
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_GE && strcasecmp(xml->elem, "range") == 0) {
		range_type = GTE;
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_LT && strcasecmp(xml->elem, "range") == 0) {
		range_type = LT;
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_LE && strcasecmp(xml->elem, "range") == 0) {
		range_type = LTE;
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_EQ && strcasecmp(xml->elem, "range") == 0) {
		range_type = EQ;
		ud->state = VULNXML_PARSE_RANGE;
	}

	if (range_type > 0) {
		vers = ud->cur_entry->packages->versions;
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

	if (strcasecmp(xml->attr, "vid") == 0)
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
		case YXML_EREF:
			pkg_emit_error("Unexpected EOF while parsing vulnxml");
			goto out;
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
		case YXML_EEOF:
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
	result = strncmp(e1->e->pkgname, e2->e->pkgname, min_len);
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
pkg_audit_preprocess(struct pkg_audit_entry *h)
{
	struct pkg_audit_entry *e;
	struct pkg_audit_item *ret;
	size_t i, n, tofill;

	n = 0;
	LL_FOREACH(h, e)
		n++;

	ret = xcalloc(n + 1, sizeof(ret[0]));
	n = 0;
	LL_FOREACH(h, e) {
		if (e->pkgname != NULL) {
			ret[n].e = e;
			ret[n].noglob_len = pkg_audit_str_noglob_len(e->pkgname);
			ret[n].next_pfx_incr = 1;
			n++;
		}
	}

	qsort(ret, n, sizeof(*ret), pkg_audit_entry_cmp);

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
		} else if (strcmp(ret[n - 1].e->pkgname,
		    ret[n].e->pkgname) == 0) {
			tofill++;
		} else {
			tofill = 1;
		}
	}

	/* Calculate jump indexes for the first byte of the package name */
	bzero(audit_entry_first_byte_idx, sizeof(audit_entry_first_byte_idx));
	for (n = 1, i = 0; n < 256; n++) {
		while (ret[i].e != NULL &&
		    (size_t)(ret[i].e->pkgname[0]) < n)
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
pkg_audit_add_entry(struct pkg_audit_entry *e, struct pkg_audit_issues **ai)
{
	struct pkg_audit_issue *issue;

	if (*ai == NULL)
		*ai = xcalloc(1, sizeof(**ai));
	issue = xcalloc(1, sizeof(*issue));
	issue->audit = e;
	(*ai)->count++;
	LL_APPEND((*ai)->issues, issue);
}

bool
pkg_audit_is_vulnerable(struct pkg_audit *audit, struct pkg *pkg,
    struct pkg_audit_issues **ai, bool stop_quick)
{
	struct pkg_audit_entry *e;
	struct pkg_audit_versions_range *vers;
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

		/*
		 * Audit entries are sorted, so if we had found one
		 * that is lexicographically greater than our name,
		 * it and the rest won't match our name.
		 */
		cmp = strncmp(pkg->name, e->pkgname, a->noglob_len);
		if (cmp > 0)
			continue;
		else if (cmp < 0)
			break;

		for (i = 0; i < a->next_pfx_incr; i++) {
			e = a[i].e;
			if (fnmatch(e->pkgname, pkg->name, 0) != 0)
				continue;

			if (pkg->version == NULL) {
				/*
				 * Assume that all versions should be checked
				 */
				res = true;
				pkg_audit_add_entry(e, ai);
			}
			else {
				LL_FOREACH(e->versions, vers) {
					res1 = pkg_audit_version_match(pkg->version, &vers->v1);
					res2 = pkg_audit_version_match(pkg->version, &vers->v2);

					if (res1 && res2) {
						res = true;
						pkg_audit_add_entry(e, ai);
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

	audit->items = pkg_audit_preprocess(audit->entries);
	audit->parsed = true;

	return (EPKG_OK);
}

void
pkg_audit_free (struct pkg_audit *audit)
{
	if (audit != NULL) {
		if (audit->parsed) {
			pkg_audit_free_list(audit->entries);
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
	struct pkg_audit_issue *i, *issue;

	if (issues == NULL)
		return;

	LL_FOREACH_SAFE(issues->issues, issue, i) {
		LL_DELETE(issues->issues, issue);
		free(issue);
	}
}
