/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <yaml.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

struct keyword {
	const char *keyword;
	STAILQ_HEAD(actions, action) actions;
	STAILQ_ENTRY(keyword) next;
};

struct plist {
	char *last_file;
	const char *stage;
	const char *prefix;
	struct sbuf *unexec_buf;
	struct sbuf *pre_install_buf;
	struct sbuf *post_install_buf;
	struct sbuf *pre_deinstall_buf;
	struct sbuf *post_deinstall_buf;
	struct sbuf *pre_upgrade_buf;
	struct sbuf *post_upgrade_buf;
	struct pkg *pkg;
	const char *uname;
	const char *gname;
	const char *slash;
	bool ignore_next;
	int64_t flatsize;
	struct hardlinks *hardlinks;
	mode_t perm;
	STAILQ_HEAD(keywords, keyword) keywords;
};

struct action {
	int (*perform)(struct plist *, char *);
	STAILQ_ENTRY(action) next;
};

static int setprefix(struct plist *, char *);
static int dirrm(struct plist *, char *);
static int dirrmtry(struct plist *, char *);
static int file(struct plist *, char *);
static int setmod(struct plist *, char *);
static int setowner(struct plist *, char *);
static int setgroup(struct plist *, char *);
static int ignore_next(struct plist *, char *);
static int ignore(struct plist *, char *);

static struct action_cmd {
	const char *name;
	int (*perform)(struct plist *, char *);
} list_actions[] = {
	{ "setprefix", setprefix },
	{ "dirrm", dirrm },
	{ "dirrmtry", dirrm },
	{ "file", file },
	{ "setmode", setmod },
	{ "setowner", setowner },
	{ "setgroup", setgroup },
	{ "ignore", ignore },
	{ "ignore_next", ignore_next },
	{ NULL, NULL }
};

static void
sbuf_append(struct sbuf *buf, __unused const char *comment, const char *str, ...)
{
	va_list ap;

	va_start(ap, str);
	sbuf_vprintf(buf, str, ap);
	va_end(ap);
}

#define post_unexec_append(buf, str, ...) \
	sbuf_append(buf, "unexec", str, __VA_ARGS__)
#define pre_unexec_append(buf, str, ...) \
	sbuf_append(buf, "unexec", str, __VA_ARGS__)
#define exec_append(buf, str, ...) \
	sbuf_append(buf, "exec", str, __VA_ARGS__)

static int
setprefix(struct plist *p, char *line)
{
	/* if no arguments then set default prefix */
	if (line[0] == '\0')
		pkg_get(p->pkg, PKG_PREFIX, &p->prefix);
	else
		p->prefix = line;
	p->slash = p->prefix[strlen(p->prefix) -1] == '/' ? "" : "/";

	return (EPKG_OK);
}

static int
meta_dirrm(struct plist *p, char *line, bool try)
{
	size_t len;
	char path[MAXPATHLEN];
	char stagedpath[MAXPATHLEN];
	char *testpath;
	struct stat st;
	bool developer;

	len = strlen(line);

	while (isspace(line[len - 1]))
		line[len - 1] = '\0';

	if (line[0] == '/')
		snprintf(path, sizeof(path), "%s/", line);
	else
		snprintf(path, sizeof(path), "%s%s%s/", p->prefix, p->slash,
		    line);

	testpath = path;

	if (p->stage != NULL) {
		snprintf(stagedpath, sizeof(stagedpath), "%s%s", p->stage, path);
		testpath = stagedpath;
	}

	if (lstat(testpath, &st) == 0)
		return (pkg_adddir_attr(p->pkg, path, p->uname, p->gname,
		    p->perm, try, true));

	pkg_config_bool(PKG_CONFIG_DEVELOPER_MODE, &developer);

	pkg_emit_errno("lstat", path);

	if (p->stage != NULL)
		return (EPKG_FATAL);
	if (developer) {
		pkg_emit_developer_mode("Plist error: @dirrm %s", line);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

static int
dirrm(struct plist *p, char *line)
{
	return (meta_dirrm(p, line, false));
}

static int
dirrmtry(struct plist *p, char *line)
{
	return (meta_dirrm(p, line, true));
}

static int
file(struct plist *p, char *line)
{
	size_t len;
	char path[MAXPATHLEN];
	char stagedpath[MAXPATHLEN];
	char *testpath;
	struct stat st;
	char *buf;
	bool regular = false;
	bool developer;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];

	len = strlen(line);

	while (isspace(line[len - 1]))
		line[len - 1] = '\0';

	if (line[0] == '/')
		snprintf(path, sizeof(path), "%s", line);
	else
		snprintf(path, sizeof(path), "%s%s%s", p->prefix,
		    p->slash, line);
	testpath = path;

	if (p->stage != NULL) {
		snprintf(stagedpath, sizeof(stagedpath), "%s%s", p->stage, path);
		testpath = stagedpath;
	}

	if (lstat(testpath, &st) == 0) {
		buf = NULL;
		regular = false;

		if (S_ISREG(st.st_mode))
			regular = true;

		/* special case for hardlinks */
		if (st.st_nlink > 1)
			regular = is_hardlink(p->hardlinks, &st);

		if (regular) {
			p->flatsize += st.st_size;
			sha256_file(testpath, sha256);
			buf = sha256;
		}
		return (pkg_addfile_attr(p->pkg, path, buf, p->uname, p->gname,
		    p->perm, true));
	}

	pkg_emit_errno("lstat", path);
	if (p->stage != NULL)
		return (EPKG_FATAL);
	pkg_config_bool(PKG_CONFIG_DEVELOPER_MODE, &developer);
	if (developer) {
		pkg_emit_developer_mode("Plist error, missing file: %s", line);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

static int
setmod(struct plist *p, char *line)
{
	void *set;

	p->perm = 0;

	if (line[0] == '\0')
		return (EPKG_OK);

	if ((set = setmode(line)) == NULL)
		pkg_emit_error("%s wrong mode value", line);
	else
		p->perm = getmode(set, 0);
	return (EPKG_OK);
}

static int
setowner(struct plist *p, char *line)
{
	if (line[0] == '\0')
		p->uname = NULL;
	else
		p->uname = line;

	return (EPKG_OK);
}

static int
setgroup(struct plist *p, char *line)
{
	if (line[0] == '\0')
		p->gname = NULL;
	else
		p->gname = line;

	return (EPKG_OK);
}

static int
ignore(__unused struct plist *p, __unused char *line)
{
	return (EPKG_OK);
}

static int
ignore_next(struct plist *p, __unused char *line)
{
	p->ignore_next = true;

	return (EPKG_OK);
}

static int
meta_exec(struct plist *p, char *line, bool unexec)
{
	char *cmd, *buf;
	char comment[2];
	char path[MAXPATHLEN + 1];
	regmatch_t pmatch[2];
	int ret;

	ret = format_exec_cmd(&cmd, line, p->prefix, p->last_file, NULL);
	if (ret != EPKG_OK)
		return (EPKG_OK);

	if (unexec) {
		comment[0] = '\0';
		/* workaround to detect the @dirrmtry */
		if (STARTS_WITH(cmd, "rmdir ")) {
			comment[0] = '#';
			comment[1] = '\0';
		} else if (STARTS_WITH(cmd, "/bin/rmdir ")) {
			comment[0] = '#';
			comment[1] = '\0';
		}
		/* remove the glob if any */
		if (comment[0] == '#') {
			if (strchr(cmd, '*'))
				comment[0] = '\0';

			buf = cmd;

			/* start remove mkdir -? */
			/* remove the command */
			while (!isspace(buf[0]))
				buf++;

			while (isspace(buf[0]))
				buf++;

			if (buf[0] == '-')
				comment[0] = '\0';
			/* end remove mkdir -? */
		}

		if (strstr(cmd, "rmdir") || strstr(cmd, "kldxref") ||
		    strstr(cmd, "mkfontscale") || strstr(cmd, "mkfontdir") ||
		    strstr(cmd, "fc-cache") || strstr(cmd, "fonts.dir") ||
		    strstr(cmd, "fonts.scale") ||
		    strstr(cmd, "gtk-update-icon-cache") ||
		    strstr(cmd, "update-desktop-database") ||
		    strstr(cmd, "update-mime-database")) {
			if (comment[0] != '#')
				post_unexec_append(p->post_deinstall_buf,
				    "%s%s\n", comment, cmd);
		} else {
			sbuf_printf(p->unexec_buf, "%s%s\n",comment, cmd);
		}
		if (comment[0] == '#') {
			buf = cmd;
			regex_t preg;

			/* remove the @dirrm{,try}
			 * command */
			while (!isspace(buf[0]))
				buf++;

			split_chr(buf, '|');

			if (strstr(buf, "\"/")) {
				regcomp(&preg, "[[:space:]]\"(/[^\"]+)",
				    REG_EXTENDED);
				while (regexec(&preg, buf, 2, pmatch, 0) == 0) {
					strlcpy(path, &buf[pmatch[1].rm_so],
					    pmatch[1].rm_eo - pmatch[1].rm_so + 1);
					buf+=pmatch[1].rm_eo;
					if (!strcmp(path, "/dev/null"))
						continue;
					dirrmtry(p, path);
				}
			} else {
				regcomp(&preg, "[[:space:]](/[[:graph:]/]+)",
				    REG_EXTENDED);
				while (regexec(&preg, buf, 2, pmatch, 0) == 0) {
					strlcpy(path, &buf[pmatch[1].rm_so],
					    pmatch[1].rm_eo - pmatch[1].rm_so + 1);
					buf+=pmatch[1].rm_eo;
					if (!strcmp(path, "/dev/null"))
						continue;
					dirrmtry(p, path);
				}
			}
			regfree(&preg);

		}
	} else {
		exec_append(p->post_install_buf, "%s\n", cmd);
	}
	return (EPKG_OK);
}

static int
exec(struct plist *p, char *line)
{
	return (meta_exec(p, line, false));
}

static int
unexec(struct plist *p, char *line)
{
	return (meta_exec(p, line, true));
}

static void
populate_keywords(struct plist *p)
{
	struct keyword *k;
	struct action *a;

	/* @cwd */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "cwd";
	STAILQ_INIT(&k->actions);
	a->perform = setprefix;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @ignore */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "ignore";
	STAILQ_INIT(&k->actions);
	a->perform = ignore_next;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @comment */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "comment";
	STAILQ_INIT(&k->actions);
	a->perform = ignore;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @dirrm */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "dirrm";
	STAILQ_INIT(&k->actions);
	a->perform = dirrm;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @dirrmtry */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "dirrmtry";
	STAILQ_INIT(&k->actions);
	a->perform = dirrmtry;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @mode */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "mode";
	STAILQ_INIT(&k->actions);
	a->perform = setmod;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @owner */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "owner";
	STAILQ_INIT(&k->actions);
	a->perform = setowner;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @group */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "group";
	STAILQ_INIT(&k->actions);
	a->perform = setgroup;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @exec */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "exec";
	STAILQ_INIT(&k->actions);
	a->perform = exec;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @unexec */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "unexec";
	STAILQ_INIT(&k->actions);
	a->perform = unexec;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);
}

static void
keyword_free(struct keyword *k)
{
	struct action *a;

	LIST_FREE(&k->actions, a, free);

	free(k);
}

static void
plist_free(struct plist *plist)
{
	struct keyword *k;
	LIST_FREE(&plist->keywords, k, keyword_free);
}

static int
parse_actions(yaml_document_t *doc, yaml_node_t *node, struct plist *p,
    char *line)
{
	yaml_node_item_t *item;
	yaml_node_t *val;
	int i;

	if (node->type != YAML_SEQUENCE_NODE) {
		pkg_emit_error("Malformed actions, skipping");
		return EPKG_FATAL;
	}

	item = node->data.sequence.items.start;
	while (item < node->data.sequence.items.top) {
		val = yaml_document_get_node(doc, *item);
		if (val->type != YAML_SCALAR_NODE) {
			pkg_emit_error("Skipping malformed action");
			++item;
			continue;
		}

		for (i = 0; list_actions[i].name != NULL; i++) {
			if (!strcasecmp(val->data.scalar.value,
			    list_actions[i].name)) {
				list_actions[i].perform(p, line);
				break;
			}
		}
		++item;
	}

	return (EPKG_OK);
}

static int
parse_and_apply_keyword_file(yaml_document_t *doc, yaml_node_t *node,
    struct plist *p, char *line)
{
	yaml_node_pair_t *pair;
	yaml_node_t *key, *val;
	char *cmd;

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);
		if (key->data.scalar.length <= 0) {
			++pair; /* ignore silently */
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "actions")) {
			parse_actions(doc, val, p, line);
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "pre-install")) {
			if (val->data.scalar.length != 0) {
				format_exec_cmd(&cmd, val->data.scalar.value,
				    p->prefix, p->last_file, line);
				sbuf_cat(p->pre_install_buf, cmd);
				free(cmd);
			}
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "post-install")) {
			if (val->data.scalar.length != 0) {
				format_exec_cmd(&cmd, val->data.scalar.value,
				    p->prefix, p->last_file, line);
				sbuf_cat(p->post_install_buf, cmd);
				free(cmd);
			}
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "pre-deinstall")) {
			if (val->data.scalar.length != 0) {
				format_exec_cmd(&cmd, val->data.scalar.value,
				    p->prefix, p->last_file, line);
				sbuf_cat(p->pre_deinstall_buf, cmd);
				free(cmd);
			}
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "post-deinstall")) {
			if (val->data.scalar.length != 0) {
				format_exec_cmd(&cmd, val->data.scalar.value,
				    p->prefix, p->last_file, line);
				sbuf_cat(p->post_deinstall_buf, cmd);
				free(cmd);
			}
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "pre-upgrade")) {
			if (val->data.scalar.length != 0) {
				format_exec_cmd(&cmd, val->data.scalar.value,
				    p->prefix, p->last_file, line);
				sbuf_cat(p->pre_upgrade_buf, cmd);
				free(cmd);
			}
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "post-upgrade")) {
			if (val->data.scalar.length != 0) {
				format_exec_cmd(&cmd, val->data.scalar.value,
				    p->prefix, p->last_file, line);
				sbuf_cat(p->post_upgrade_buf, cmd);
				free(cmd);
			}
			++pair;
			continue;
		}
		++pair;
	}

	return (EPKG_OK);
}

static int
external_keyword(struct plist *plist, char *keyword, char *line)
{
	const char *keyword_dir = NULL;
	char keyfile_path[MAXPATHLEN];
	FILE *fp;
	int ret = EPKG_UNKNOWN;
	yaml_parser_t parser;
	yaml_document_t doc;
	yaml_node_t *node;

	pkg_config_string(PKG_CONFIG_PLIST_KEYWORDS_DIR, &keyword_dir);
	if (keyword_dir == NULL) {
		pkg_config_string(PKG_CONFIG_PORTSDIR, &keyword_dir);
		snprintf(keyfile_path, sizeof(keyfile_path),
		    "%s/Keywords/%s.yaml", keyword_dir, keyword);
	} else {
		snprintf(keyfile_path, sizeof(keyfile_path),
		    "%s/%s.yaml", keyword_dir, keyword);
	}

	if ((fp = fopen(keyfile_path, "r")) == NULL)
		return (EPKG_UNKNOWN);

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, fp);
	yaml_parser_load(&parser, &doc);

	node = yaml_document_get_root_node(&doc);
	if (node != NULL) {
		if (node->type != YAML_MAPPING_NODE) {
			pkg_emit_error("Invalid keyword file format: %s",
			    keyfile_path);
		} else {
			ret = parse_and_apply_keyword_file(&doc, node, plist,
			    line);
		}
	} else {
		pkg_emit_error("Invalid keyword file format: %s", keyfile_path);
	}

	yaml_document_delete(&doc);
	yaml_parser_delete(&parser);

	return (ret);

}

static int
parse_keywords(struct plist *plist, char *keyword, char *line)
{
	struct keyword *k;
	struct action *a;
	int ret = EPKG_FATAL;

	STAILQ_FOREACH(k, &plist->keywords, next) {
		if (!strcmp(k->keyword, keyword)) {
			STAILQ_FOREACH(a, &k->actions, next) {
				ret = a->perform(plist, line);
				if (ret != EPKG_OK)
					return (ret);
			}
			return (ret);
		}
	}

	/*
	 * if we are it means the keyword as not been found
	 * maybe it is defined externally
	 * let's try to find it
	 */
	return (external_keyword(plist, keyword, line));
}

static void
flush_script_buffer(struct sbuf *buf, struct pkg *p, int type)
{
	if (sbuf_len(buf) > 0) {
		sbuf_finish(buf);
		pkg_appendscript(p, sbuf_get(buf), type);
	}
	sbuf_delete(buf);
}

int
ports_parse_plist(struct pkg *pkg, char *plist, const char *stage)
{
	char *plist_p, *buf, *plist_buf;
	int nbel, i;
	size_t next;
	size_t len;
	int ret = EPKG_OK;
	off_t sz = 0;
	struct hardlinks hardlinks = {NULL, 0, 0};
	struct plist pplist;

	assert(pkg != NULL);
	assert(plist != NULL);

	pplist.last_file = NULL;
	pplist.prefix = NULL;
	pplist.stage = stage;
	pplist.unexec_buf = sbuf_new_auto();
	pplist.pre_install_buf = sbuf_new_auto();
	pplist.post_install_buf = sbuf_new_auto();
	pplist.pre_deinstall_buf = sbuf_new_auto();
	pplist.post_deinstall_buf = sbuf_new_auto();
	pplist.pre_upgrade_buf = sbuf_new_auto();
	pplist.post_upgrade_buf = sbuf_new_auto();
	pplist.uname = NULL;
	pplist.gname = NULL;
	pplist.perm = 0;
	pplist.pkg = pkg;
	pplist.slash = "";
	pplist.ignore_next = false;
	pplist.hardlinks = &hardlinks;
	pplist.flatsize = 0;
	STAILQ_INIT(&pplist.keywords);

	populate_keywords(&pplist);

	buf = NULL;

	if ((ret = file_to_buffer(plist, &plist_buf, &sz)) != EPKG_OK)
		return (ret);

	pkg_get(pkg, PKG_PREFIX, &pplist.prefix);
	pplist.slash = pplist.prefix[strlen(pplist.prefix) - 1] == '/' ? "" : "/";

	nbel = split_chr(plist_buf, '\n');

	next = strlen(plist_buf);
	plist_p = plist_buf;

	for (i = 0; i <= nbel; i++) {
		if (pplist.ignore_next) {
			pplist.ignore_next = false;
			plist_p += next + 1;
			next = strlen(plist_p);
			continue;
		}

		if (plist_p[0] == '@') {
			char *keyword = plist_p;

			keyword++; /* skip the @ */
			buf = keyword;
			while (!(isspace(buf[0]) || buf[0] == '\0'))
				buf++;

			if (buf[0] != '\0') {
				buf[0] = '\0';
				buf++;
			}
			/* trim write spaces */
			while (isspace(buf[0]))
				buf++;
			switch (parse_keywords(&pplist, keyword, buf)) {
			case EPKG_UNKNOWN:
				pkg_emit_error("unknown keyword %s, ignoring %s",
				    keyword, plist_p);
				break;
			case EPKG_FATAL:
				ret = EPKG_FATAL;
				break;
			}
		} else if ((len = strlen(plist_p)) > 0){
			if (sbuf_len(pplist.unexec_buf) > 0) {
				sbuf_finish(pplist.unexec_buf);
				pre_unexec_append(pplist.pre_deinstall_buf,
				    sbuf_get(pplist.unexec_buf), "");
				sbuf_reset(pplist.unexec_buf);
			}
			buf = plist_p;
			pplist.last_file = buf;

			/* remove spaces at the begining and at the end */
			while (isspace(buf[0]))
				buf++;

			if (file(&pplist, buf) != EPKG_OK)
				ret = EPKG_FATAL;
		}

		if (i != nbel) {
			plist_p += next + 1;
			next = strlen(plist_p);
		}
	}

	pkg_set(pkg, PKG_FLATSIZE, pplist.flatsize);

	flush_script_buffer(pplist.pre_install_buf, pkg,
	    PKG_SCRIPT_PRE_INSTALL);
	flush_script_buffer(pplist.post_install_buf, pkg,
	    PKG_SCRIPT_POST_INSTALL);
	flush_script_buffer(pplist.pre_deinstall_buf, pkg,
	    PKG_SCRIPT_PRE_DEINSTALL);
	flush_script_buffer(pplist.unexec_buf, pkg,
	    PKG_SCRIPT_POST_DEINSTALL);
	flush_script_buffer(pplist.post_deinstall_buf, pkg,
	    PKG_SCRIPT_POST_DEINSTALL);
	flush_script_buffer(pplist.pre_upgrade_buf, pkg,
	    PKG_SCRIPT_PRE_UPGRADE);
	flush_script_buffer(pplist.post_upgrade_buf, pkg,
	    PKG_SCRIPT_POST_UPGRADE);

	free(hardlinks.inodes);

	free(plist_buf);
	plist_free(&pplist);

	return (ret);
}
