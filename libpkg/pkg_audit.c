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
#include <ucl.h>

#include <yxml.h>

#ifdef __linux__
# ifdef __GLIBC__
#  include <sys/time.h>
# endif
#endif

#include "pkg.h"
#include "pkg/audit.h"
#include "private/pkg.h"
#include <private/pkg_osvf.h>
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
	struct ucl_parser *parser;
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
pkg_audit_free_list(struct pkg_audit_entry *h)
{
	struct pkg_audit_entry *e;

	while (h) {
		e = h;
		h = h->next;
		if(!e->pkgname) {
			pkg_osvf_free_entry(e);
		} else {
			free(e);
		}
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

	if(!pkg_config_get("OSVF_SITE"))
	{
		pkg_emit_notice("There is not OSVF_SITE config key available. Can't continue");
		return retcode;
	}

	if (src == NULL) {
		src = pkg_object_string(pkg_config_get("OSVF_SITE"));
	}

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";

	strlcpy(tmp, tmpdir, sizeof(tmp));
	strlcat(tmp, "/freebsd-osv.json.XXXXXXXXXX", sizeof(tmp));

	if (dest != NULL) {
		if (stat(dest, &st) != -1)
			t = st.st_mtime;
	} else {
		dfd = pkg_get_dbdirfd();
		if (fstatat(dfd, "freebsd-osv.json", &st, 0) != -1)
			t = st.st_mtime;
	}

	switch (pkg_fetch_file_tmp(NULL, src, tmp, t)) {
	case EPKG_OK:
		break;
	case EPKG_UPTODATE:
		pkg_emit_notice("OSVF database file up-to-date");
		retcode = EPKG_OK;
		goto cleanup;
	default:
		pkg_emit_error("cannot fetch OSVF database file");
		goto cleanup;
	}

	/* Open input fd */
	fd = open(tmp, O_RDONLY);
	if (fd == -1)
		goto cleanup;
	/* Open out fd */
	if (dest != NULL) {
		outfd = open(dest, O_RDWR|O_CREAT|O_TRUNC,
		    S_IRUSR|S_IRGRP|S_IROTH);
	} else {
		outfd = openat(dfd, "freebsd-osv.json", O_RDWR|O_CREAT|O_TRUNC,
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
		pkg_osvf_free_entry(entry);
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
		} else if (STREQ(ret[n - 1].e->pkgname, ret[n].e->pkgname)) {
			tofill++;
		} else {
			tofill = 1;
		}
	}

	/* Calculate jump indexes for the first byte of the package name */
	memset(audit_entry_first_byte_idx, '\0', sizeof(audit_entry_first_byte_idx));
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

	if(!audit)
	{
		return NULL;
	}

	audit->parser = ucl_parser_new(0);

	return (audit);
}

int
pkg_audit_load(struct pkg_audit *audit, const char *fname)
{
	int dfd, fd;
	struct stat st;

	if (fname != NULL) {
		if ((fd = open(fname, O_RDONLY)) == -1)
			return (EPKG_FATAL);
	} else {
		dfd = pkg_get_dbdirfd();
		if ((fd = openat(dfd, "freebsd-osv.json", O_RDONLY)) == -1)
			return (EPKG_FATAL);
	}

	if (fstat(fd, &st) == -1) {
		close(fd);
		return (EPKG_FATAL);
	}

	/*
	 * Parse JSON which should be an array containing single
	 * OSV compatible vulnerability as an object
	 */
	if (!ucl_parser_add_fd(audit->parser, fd))
	{
		pkg_emit_error("Error parsing UCL file '%s': %s'",
		               fname, ucl_parser_get_error(audit->parser));
		close(fd);
		return (EPKG_FATAL);
	}
	close(fd);

	audit->loaded = true;

	return (EPKG_OK);
}

/* This can and should be executed after cap_enter(3) */
int
pkg_audit_process(struct pkg_audit *audit)
{
	ucl_object_t *root_obj = NULL;
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	struct pkg_audit_entry *cur_entry = NULL;
	struct ucl_schema_error err;

	if (geteuid() == 0)
		return (EPKG_FATAL);

	if (!audit->loaded)
		return (EPKG_FATAL);

	root_obj = ucl_parser_get_object(audit->parser);
	ucl_parser_free(audit->parser);
	audit->parser = NULL;

	if (root_obj == NULL)
	{
		pkg_emit_error("JSON cannot be parsed: %s", err.msg);
		return (EPKG_FATAL);
	}

	if(root_obj && ucl_object_type(root_obj) == UCL_ARRAY)
	{
		while ((cur = ucl_iterate_object(root_obj, &it, true)))
		{
			if(cur && ucl_object_type(cur) == UCL_OBJECT)
			{
				cur_entry = pkg_osvf_create_entry((ucl_object_t *)cur);

				if(!cur_entry)
				{
					return (EPKG_FATAL);
				}

				pkg_audit_expand_entry(cur_entry, &audit->entries);
				cur_entry->pkgname = NULL;
			}
		}
	}
	else
	{
		return (EPKG_FATAL);
	}

	audit->items = pkg_audit_preprocess(audit->entries);
	audit->parsed = true;
	ucl_object_unref(root_obj);

	return (EPKG_OK);
}

void
pkg_audit_free (struct pkg_audit *audit)
{
	if (audit != NULL) {
		ucl_parser_free(audit->parser);
		free(audit->items);
		pkg_audit_free_list(audit->entries);
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
