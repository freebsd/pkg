/*-
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
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
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define _WITH_GETLINE

#include <archive.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <utlist.h>

#include <expat.h>

#include <pkg.h>
#include "pkgcli.h"

#define EQ 1
#define LT 2
#define LTE 3
#define GT 4
#define GTE 5
struct version_entry {
	char *version;
	int type;
};

struct audit_versions {
	struct version_entry v1;
	struct version_entry v2;
	struct audit_versions *next;
};

struct audit_cve {
	char *cvename;
	struct audit_cve *next;
};

struct audit_pkgname_entry {
	char *pkgname;
	struct audit_pkgname_entry *next;
};

struct audit_entry {
	const char *pkgname;
	struct audit_pkgname_entry *names;
	struct audit_versions *versions;
	struct audit_cve *cve;
	char *url;
	char *desc;
	char *id;
	bool ref;
	struct audit_entry *next;
};

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
struct audit_entry_sorted {
	struct audit_entry *e;	/* Entry itself */
	size_t noglob_len;	/* Prefix without glob characters */
	size_t next_pfx_incr;	/* Index increment for the entry with
				   different prefix */
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

void
usage_audit(void)
{
	fprintf(stderr, "Usage: pkg audit [-Fq] <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help audit'.\n");
}

static int
fetch_and_extract(const char *src, const char *dest)
{
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;
	int fd = -1;
	char tmp[MAXPATHLEN];
	const char *tmpdir;
	int retcode = EPKG_FATAL;
	int ret;
	time_t t = 0;
	struct stat st;

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";
	strlcpy(tmp, tmpdir, sizeof(tmp));
	strlcat(tmp, "/vuln.xml.bz2.XXXX", sizeof(tmp));
	if (stat(dest, &st) != -1) {
		t = st.st_mtime;
	}
	switch (pkg_fetch_file(NULL, src, tmp, t)) {
	case EPKG_OK:
		break;
	case EPKG_UPTODATE:
		printf("Vulnxml file up-to-date.\n");
		retcode = EPKG_OK;
		goto cleanup;
	default:
		warnx("Cannot fetch vulnxml file!");
		goto cleanup;
	}

	a = archive_read_new();
#if ARCHIVE_VERSION_NUMBER < 3000002
	archive_read_support_compression_all(a);
#else
	archive_read_support_filter_all(a);
#endif

	archive_read_support_format_raw(a);

	if (archive_read_open_filename(a, tmp, 4096) != ARCHIVE_OK) {
		warnx("archive_read_open_filename(%s): %s",
				tmp, archive_error_string(a));
		goto cleanup;
	}

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
		fd = open(dest, O_RDWR|O_CREAT|O_TRUNC,
				S_IRUSR|S_IRGRP|S_IROTH);
		if (fd < 0) {
			warn("open(%s)", dest);
			goto cleanup;
		}

		if (archive_read_data_into_fd(a, fd) != ARCHIVE_OK) {
			warnx("archive_read_data_into_fd(%s): %s",
					dest, archive_error_string(a));
			goto cleanup;
		}
	}

	retcode = EPKG_OK;

	cleanup:
	unlink(tmp);
	if (a != NULL) {
		archive_read_close(a);
		archive_read_free(a);
	}
	if (fd >= 0)
		close(fd);

	return (retcode);
}

/*
 * Expand multiple names to a set of audit entries
 */
static void
audit_expand_entries(struct audit_entry *entry, struct audit_entry *head)
{
	struct audit_entry *n;
	struct audit_pkgname_entry *pcur;

	/* Set the name of the current entry */
	if (entry->names == NULL)
		return;

	entry->pkgname = entry->names->pkgname;

	if (entry->names->next == NULL) {
		/* Nothing to expand */
		return;
	}

	LL_FOREACH(entry->names->next, pcur) {
		n = calloc(1, sizeof(struct audit_entry));
		if (n == NULL)
			err(1, "calloc(audit_entry)");
		n->pkgname = pcur->pkgname;
		/* Set new entry as reference entry */
		n->ref = true;
		n->cve = entry->cve;
		n->desc = entry->desc;
		n->versions = entry->versions;
		n->url = entry->url;
		n->id = entry->id;
		LL_PREPEND(head, n);
	}
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

struct vulnxml_userdata {
	struct audit_entry *h;
	struct audit_entry *cur_entry;
	enum vulnxml_parse_state state;
	int range_num;
};

static void
vulnxml_start_element(void *data, const char *element, const char **attributes)
{
	struct vulnxml_userdata *ud = (struct vulnxml_userdata *)data;
	struct audit_versions *vers;
	struct audit_pkgname_entry *name_entry;
	int i;

	if (ud->state == VULNXML_PARSE_INIT && strcasecmp(element, "vuln") == 0) {
		ud->cur_entry = calloc(1, sizeof(struct audit_entry));
		if (ud->cur_entry == NULL)
			err(1, "calloc(audit_entry)");
		for (i = 0; attributes[i]; i += 2) {
			if (strcasecmp(attributes[i], "vid") == 0) {
				ud->cur_entry->id = strdup(attributes[i + 1]);
				break;
			}
		}
		ud->cur_entry->next = ud->h;
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_VULN && strcasecmp(element, "topic") == 0) {
		ud->state = VULNXML_PARSE_TOPIC;
	}
	else if (ud->state == VULNXML_PARSE_VULN && strcasecmp(element, "package") == 0) {
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_VULN && strcasecmp(element, "cvename") == 0) {
		ud->state = VULNXML_PARSE_CVE;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && strcasecmp(element, "name") == 0) {
		ud->state = VULNXML_PARSE_PACKAGE_NAME;
		name_entry = calloc(1, sizeof(struct audit_pkgname_entry));
		if (name_entry == NULL)
			err(1, "calloc(audit_pkgname_entry)");
		LL_PREPEND(ud->cur_entry->names, name_entry);
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && strcasecmp(element, "range") == 0) {
		ud->state = VULNXML_PARSE_RANGE;
		vers = calloc(1, sizeof(struct audit_versions));
		if (vers == NULL)
			err(1, "calloc(audit_versions)");
		LL_PREPEND(ud->cur_entry->versions, vers);
		ud->range_num = 0;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(element, "gt") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_GT;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(element, "ge") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_GE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(element, "lt") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_LT;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(element, "le") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_LE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(element, "eq") == 0) {
		ud->range_num ++;
		ud->state = VULNXML_PARSE_RANGE_EQ;
	}
}

static void
vulnxml_end_element(void *data, const char *element)
{
	struct vulnxml_userdata *ud = (struct vulnxml_userdata *)data;

	if (ud->state == VULNXML_PARSE_VULN && strcasecmp(element, "vuln") == 0) {
		audit_expand_entries(ud->cur_entry, ud->h);
		if (ud->cur_entry->pkgname != NULL) {
			LL_PREPEND(ud->h, ud->cur_entry);
		}
		else {
			/* Ignore canceled entries */
			/* XXX: can memory leak here */
			if (ud->cur_entry->id != NULL)
				free(ud->cur_entry->id);
			free(ud->cur_entry);
		}
		ud->state = VULNXML_PARSE_INIT;
	}
	else if (ud->state == VULNXML_PARSE_TOPIC && strcasecmp(element, "topic") == 0) {
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_CVE && strcasecmp(element, "cvename") == 0) {
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE && strcasecmp(element, "package") == 0) {
		ud->state = VULNXML_PARSE_VULN;
	}
	else if (ud->state == VULNXML_PARSE_PACKAGE_NAME && strcasecmp(element, "name") == 0) {
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE && strcasecmp(element, "range") == 0) {
		ud->state = VULNXML_PARSE_PACKAGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_GT && strcasecmp(element, "gt") == 0) {
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_GE && strcasecmp(element, "ge") == 0) {
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_LT && strcasecmp(element, "lt") == 0) {
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_LE && strcasecmp(element, "le") == 0) {
		ud->state = VULNXML_PARSE_RANGE;
	}
	else if (ud->state == VULNXML_PARSE_RANGE_EQ && strcasecmp(element, "eq") == 0) {
		ud->state = VULNXML_PARSE_RANGE;
	}
}

static void
vulnxml_handle_data(void *data, const char *content, int length)
{
	struct vulnxml_userdata *ud = (struct vulnxml_userdata *)data;
	struct audit_versions *vers;
	struct audit_cve *cve;
	struct audit_entry *entry;
	int range_type = -1;

	switch(ud->state) {
	case VULNXML_PARSE_INIT:
	case VULNXML_PARSE_VULN:
	case VULNXML_PARSE_PACKAGE:
	case VULNXML_PARSE_RANGE:
		/* On these states we do not need any data */
		break;
	case VULNXML_PARSE_TOPIC:
		ud->cur_entry->desc = strndup(content, length);
		break;
	case VULNXML_PARSE_PACKAGE_NAME:
		ud->cur_entry->names->pkgname = strndup(content, length);
		break;
	case VULNXML_PARSE_RANGE_GT:
		range_type = GT;
		break;
	case VULNXML_PARSE_RANGE_GE:
		range_type = GTE;
		break;
	case VULNXML_PARSE_RANGE_LT:
		range_type = LT;
		break;
	case VULNXML_PARSE_RANGE_LE:
		range_type = LTE;
		break;
	case VULNXML_PARSE_RANGE_EQ:
		range_type = EQ;
		break;
	case VULNXML_PARSE_CVE:
		entry = ud->cur_entry;
		cve = malloc(sizeof(struct audit_cve));
		cve->cvename = strndup(content, length);
		LL_PREPEND(entry->cve, cve);
		break;
	}

	if (range_type > 0) {
		vers = ud->cur_entry->versions;
		if (ud->range_num == 1) {
			vers->v1.version = strndup(content, length);
			vers->v1.type = range_type;
		}
		else if (ud->range_num == 2) {
			vers->v2.version = strndup(content, length);
			vers->v2.type = range_type;
		}
	}
}

static int
parse_db_vulnxml(const char *path, struct audit_entry **h)
{
	int fd;
	void *mem;
	struct stat st;
	XML_Parser parser;
	struct vulnxml_userdata ud;
	int ret = EPKG_OK;

	if (stat(path, &st) == -1)
		return (EPKG_FATAL);

	if ((fd = open(path, O_RDONLY)) == -1)
		return (EPKG_FATAL);

	if ((mem = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		close(fd);
		return (EPKG_FATAL);
	}
	close(fd);

	parser = XML_ParserCreate(NULL);
	XML_SetElementHandler(parser, vulnxml_start_element, vulnxml_end_element);
	XML_SetCharacterDataHandler(parser, vulnxml_handle_data);
	XML_SetUserData(parser, &ud);

	ud.cur_entry = NULL;
	ud.h = *h;
	ud.range_num = 0;
	ud.state = VULNXML_PARSE_INIT;

	if (XML_Parse(parser, mem, st.st_size, XML_TRUE) == XML_STATUS_ERROR) {
	    warnx("vulnxml parsing error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
	}

	XML_ParserFree(parser);
	munmap(mem, st.st_size);

	*h = ud.h;

	return (ret);
}

/*
 * Returns the length of the largest prefix without globbing
 * characters, as per fnmatch().
 */
static size_t
str_noglob_len(const char *s)
{
	size_t n;

	for (n = 0; s[n] && s[n] != '*' && s[n] != '?' &&
	    s[n] != '[' && s[n] != '{' && s[n] != '\\'; n++);

	return n;
}

/*
 * Helper for quicksort that lexicographically orders prefixes.
 */
static int
audit_entry_compare(const void *a, const void *b)
{
	const struct audit_entry_sorted *e1, *e2;
	size_t min_len;
	int result;

	e1 = (const struct audit_entry_sorted *)a;
	e2 = (const struct audit_entry_sorted *)b;

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
static struct audit_entry_sorted *
preprocess_db(struct audit_entry *h)
{
	struct audit_entry *e;
	struct audit_entry_sorted *ret;
	size_t i, n, tofill;

	n = 0;
	LL_FOREACH(h, e)
		n++;

	ret = (struct audit_entry_sorted *)calloc(n + 1, sizeof(ret[0]));
	if (ret == NULL)
		err(1, "calloc(audit_entry_sorted*)");
	bzero((void *)ret, (n + 1) * sizeof(ret[0]));

	n = 0;
	LL_FOREACH(h, e) {
		ret[n].e = e;
		ret[n].noglob_len = str_noglob_len(e->pkgname);
		ret[n].next_pfx_incr = 1;
		n++;
	}

	qsort(ret, n, sizeof(*ret), audit_entry_compare);

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
			struct audit_entry_sorted *base;

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
match_version(const char *pkgversion, struct version_entry *v)
{
	bool res = false;

	/*
	 * Return true so it is easier for the caller to handle case where there is
	 * only one version to match: the missing one will always match.
	 */
	if (v->version == NULL)
		return true;

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
	return res;
}

static bool
is_vulnerable(struct audit_entry_sorted *a, struct pkg *pkg)
{
	struct audit_entry *e;
	struct audit_versions *vers;
	struct audit_cve *cve;
	const char *pkgname;
	const char *pkgversion;
	bool res = false, res1, res2;

	pkg_get(pkg,
		PKG_NAME, &pkgname,
		PKG_VERSION, &pkgversion
	);

	a += audit_entry_first_byte_idx[(size_t)pkgname[0]];
	for (; (e = a->e) != NULL; a += a->next_pfx_incr) {
		int cmp;
		size_t i;

		/*
		 * Audit entries are sorted, so if we had found one
		 * that is lexicographically greater than our name,
		 * it and the rest won't match our name.
		 */
		cmp = strncmp(pkgname, e->pkgname, a->noglob_len);
		if (cmp > 0)
			continue;
		else if (cmp < 0)
			break;

		for (i = 0; i < a->next_pfx_incr; i++) {
			e = a[i].e;
			if (fnmatch(e->pkgname, pkgname, 0) != 0)
				continue;

			LL_FOREACH(e->versions, vers) {
				res1 = match_version(pkgversion, &vers->v1);
				res2 = match_version(pkgversion, &vers->v2);
				if (res1 && res2) {
					res = true;
					if (quiet) {
						printf("%s-%s\n", pkgname, pkgversion);
						return res; /* avoid reporting the same pkg multiple times */
					} else {
						printf("%s-%s is vulnerable:\n", pkgname, pkgversion);
						printf("%s\n", e->desc);
						/* XXX: for vulnxml we should use more clever approach indeed */
						if (e->cve) {
							cve = e->cve;
							while (cve) {
								printf("CVE: %s\n", cve->cvename);
								cve = cve->next;
							}
						}
						if (e->url)
							printf("WWW: %s\n\n", e->url);
						else if (e->id)
							printf("WWW: http://portaudit.FreeBSD.org/%s.html\n\n", e->id);
					}
					break;
				}
			}
		}
	}

	return res;
}

static void
free_audit_list(struct audit_entry *h)
{
	struct audit_entry *e;
	struct audit_versions *vers, *vers_tmp;
	struct audit_cve *cve, *cve_tmp;
	struct audit_pkgname_entry *pname, *pname_tmp;

	while (h) {
		e = h;
		h = h->next;
		if (!e->ref) {
			LL_FOREACH_SAFE(e->versions, vers, vers_tmp) {
				if (vers->v1.version) {
					free(vers->v1.version);
				}
				if (vers->v2.version) {
					free(vers->v2.version);
				}
				free(vers);
			}
			LL_FOREACH_SAFE(e->cve, cve, cve_tmp) {
				if (cve->cvename)
					free(cve->cvename);
				free(cve);
			}
			LL_FOREACH_SAFE(e->names, pname, pname_tmp) {
				if (pname->pkgname)
					free(pname->pkgname);
				free(pname);
			}
			if (e->url)
				free(e->url);
			if (e->desc)
				free(e->desc);
			if (e->id)
				free(e->id);
		}
		free(e);
	}
}

int
exec_audit(int argc, char **argv)
{
	struct audit_entry		*h = NULL;
	struct audit_entry_sorted	*cooked_audit_entries = NULL;
	struct pkgdb			*db = NULL;
	struct pkgdb_it			*it = NULL;
	struct pkg			*pkg = NULL;
	const char			*db_dir;
	char				*name;
	char				*version;
	char				 audit_file_buf[MAXPATHLEN];
	char				*audit_file = audit_file_buf;
	unsigned int			 vuln = 0;
	bool				 fetch = false;
	int				 ch;
	int				 ret = EX_OK, res;
	const char			*portaudit_site = NULL;

	db_dir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	snprintf(audit_file_buf, sizeof(audit_file_buf), "%s/vuln.xml", db_dir);

	while ((ch = getopt(argc, argv, "qFf:")) != -1) {
		switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 'F':
			fetch = true;
			break;
		case 'f':
			audit_file = optarg;
			break;
		default:
			usage_audit();
			return(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (fetch == true) {
		portaudit_site = pkg_object_string(pkg_config_get("VULNXML_SITE"));
		if (fetch_and_extract(portaudit_site, audit_file) != EPKG_OK) {
			return (EX_IOERR);
		}
	}

	if (argc > 2) {
		usage_audit();
		return (EX_USAGE);
	}

	if (argc == 1) {
		name = argv[0];
		version = strrchr(name, '-');
		if (version == NULL)
			err(EX_USAGE, "bad package name format: %s", name);
		version[0] = '\0';
		version++;
		if (pkg_new(&pkg, PKG_FILE) != EPKG_OK)
			err(EX_OSERR, "malloc");
		pkg_set(pkg,
		    PKG_NAME, name,
		    PKG_VERSION, version);
		res = parse_db_vulnxml(audit_file, &h);
		if (res != EPKG_OK) {
			if (errno == ENOENT)
				warnx("vulnxml file %s does not exist. "
				      "Try running 'pkg audit -F' first",
				      audit_file);
			else
				warn("unable to open vulnxml file %s",
				     audit_file);
			ret = EX_DATAERR;
			goto cleanup;
		}
		cooked_audit_entries = preprocess_db(h);
		is_vulnerable(cooked_audit_entries, pkg);
		goto cleanup;
	}

	/*
	 * if the database doesn't exist it just means there are no
	 * packages to audit.
	 */

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	if (ret == EPKG_ENODB) 
		return (EX_OK);
	else if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to read the package database");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EX_IOERR);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY, 0, 0) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL)
	{
		warnx("Error accessing the package database");
		ret = EX_IOERR;
		goto cleanup;
	}

	res = parse_db_vulnxml(audit_file, &h);
	if (res != EPKG_OK) {
		if (errno == ENOENT)
			warnx("unable to open vulnxml file, try running 'pkg audit -F' first");
		else
			warn("unable to open vulnxml file %s", audit_file);
		ret = EX_DATAERR;
		goto cleanup;
	}
	cooked_audit_entries = preprocess_db(h);

	while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK)
		if (is_vulnerable(cooked_audit_entries, pkg))
			vuln++;

	if (ret == EPKG_END && vuln == 0)
		ret = EX_OK;

	if (!quiet)
		printf("%u problem(s) in the installed packages found.\n", vuln);

cleanup:
	pkgdb_it_free(it);
	if (db != NULL)
		pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);
	pkg_free(pkg);
	free_audit_list(h);

	return (ret);
}
