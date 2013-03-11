/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2012-2013 Bryan Drewery <bdrewery@FreeBSD.org>
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
#include <stringlist.h>
#include <unistd.h>
#ifdef BUNDLED_YAML
#include <yaml.h>
#else
#include <bsdyml.h>
#endif
#include <uthash.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

struct keyword {
	/* 64 is more than enough for this */
	char keyword[64];
	struct action *actions;
	UT_hash_handle hh;
};

struct plist {
	char *last_file;
	const char *stage;
	const char *prefix;
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
	char *pkgdep;
	bool ignore_next;
	int64_t flatsize;
	struct hardlinks *hardlinks;
	mode_t perm;
	char *post_pattern_to_free;
	StringList *post_patterns;
	struct keyword *keywords;
};

struct file_attr {
	char *owner;
	char *group;
	mode_t mode;
};

struct action {
	int (*perform)(struct plist *, char *, struct file_attr *);
	struct action *next;
};

static int setprefix(struct plist *, char *, struct file_attr *);
static int dirrm(struct plist *, char *, struct file_attr *);
static int dirrmtry(struct plist *, char *, struct file_attr *);
static int file(struct plist *, char *, struct file_attr *);
static int setmod(struct plist *, char *, struct file_attr *);
static int setowner(struct plist *, char *, struct file_attr *);
static int setgroup(struct plist *, char *, struct file_attr *);
static int ignore_next(struct plist *, char *, struct file_attr *);
static int comment_key(struct plist *, char *, struct file_attr *);
/* compat with old packages */
static int name_key(struct plist *, char *, struct file_attr *);
static int pkgdep(struct plist *, char *, struct file_attr *);

static struct action_cmd {
	const char *name;
	int (*perform)(struct plist *, char *, struct file_attr *);
} list_actions[] = {
	{ "setprefix", setprefix },
	{ "dirrm", dirrm },
	{ "dirrmtry", dirrm },
	{ "file", file },
	{ "setmode", setmod },
	{ "setowner", setowner },
	{ "setgroup", setgroup },
	{ "comment", comment_key },
	{ "ignore_next", ignore_next },
	/* compat with old packages */
	{ "name", name_key },
	{ "pkgdep", pkgdep },
	{ NULL, NULL }
};

static void
free_file_attr(struct file_attr *a)
{
	if (a == NULL)
		return;
	free(a->owner);
	free(a->group);
	free(a);
}

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
setprefix(struct plist *p, char *line, struct file_attr *a)
{
	char *pkgprefix;

	/* if no arguments then set default prefix */
	if (line[0] == '\0')
		pkg_get(p->pkg, PKG_PREFIX, &p->prefix);
	else
		p->prefix = line;

	pkg_get(p->pkg, PKG_PREFIX, &pkgprefix);
	if (pkgprefix == NULL || *pkgprefix == '\0')
		pkg_set(p->pkg, PKG_PREFIX, line);

	p->slash = p->prefix[strlen(p->prefix) -1] == '/' ? "" : "/";

	exec_append(p->post_install_buf, "cd %s\n", p->prefix);
	pre_unexec_append(p->pre_deinstall_buf, "cd %s\n", p->prefix);
	post_unexec_append(p->post_deinstall_buf, "cd %s\n", p->prefix);

	free(a);

	return (EPKG_OK);
}

static int
name_key(struct plist *p, char *line, struct file_attr *a)
{
	char *name;
	char *tmp;

	pkg_get(p->pkg, PKG_NAME, &name);
	if (name != NULL && *name != '\0') {
		free(a);
		return (EPKG_OK);
	}
	tmp = strrchr(line, '-');
	tmp[0] = '\0';
	tmp++;
	pkg_set(p->pkg, PKG_NAME, line, PKG_VERSION, tmp);

	free(a);
	return (EPKG_OK);
}

static int
pkgdep(struct plist *p, char *line, struct file_attr *a)
{
	if (*line != '\0')
		p->pkgdep = line;
	free(a);
	return (EPKG_OK);
}

static int
meta_dirrm(struct plist *p, char *line, struct file_attr *a, bool try)
{
	size_t len;
	char path[MAXPATHLEN];
	char stagedpath[MAXPATHLEN];
	char *testpath;
	struct stat st;
	bool developer;
	int ret;

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

	if (lstat(testpath, &st) == -1) {
		pkg_emit_errno("lstat", path);
		if (p->stage != NULL)
			ret = EPKG_FATAL;
		pkg_config_bool(PKG_CONFIG_DEVELOPER_MODE, &developer);
		if (developer) {
			pkg_emit_developer_mode("Plist error: @dirrm %s", line);
			ret = EPKG_FATAL;
		}
	} else {
		if (a != NULL)
			ret = pkg_adddir_attr(p->pkg, path,
			    a->owner ? a->owner : p->uname,
			    a->group ? a->group : p->gname,
			    a->mode ? a->mode : p->perm,
			    try, true);
		else
			ret = pkg_adddir_attr(p->pkg, path, p->uname, p->gname,
			    p->perm, try, true);
	}

	free_file_attr(a);
	return (ret);
}

static int
dirrm(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_dirrm(p, line, a, false));
}

static int
dirrmtry(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_dirrm(p, line, a, true));
}

static int
file(struct plist *p, char *line, struct file_attr *a)
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
	int ret;

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

	if (lstat(testpath, &st) == -1) {
		pkg_emit_errno("lstat", path);
		if (p->stage != NULL)
			ret = EPKG_FATAL;
		pkg_config_bool(PKG_CONFIG_DEVELOPER_MODE, &developer);
		if (developer) {
			pkg_emit_developer_mode("Plist error, missing file: %s", line);
			ret = EPKG_FATAL;
		}
	} else {
		buf = NULL;
		regular = false;

		if (S_ISREG(st.st_mode))
			regular = true;

		/* special case for hardlinks */
		if (st.st_nlink > 1)
			regular = is_hardlink(p->hardlinks, &st);

		if (regular) {
			p->flatsize += st.st_size;
			if (pkg_type(p->pkg) == PKG_OLD_FILE)
				md5_file(testpath, sha256);
			else
				sha256_file(testpath, sha256);
			buf = sha256;
		}
		if (a != NULL)
			ret = pkg_addfile_attr(p->pkg, path, buf,
			    a->owner ? a->owner : p->uname,
			    a->group ? a->group : p->gname,
			    a->mode ? a->mode : p->perm, true);
		else
			ret = pkg_addfile_attr(p->pkg, path, buf, p->uname,
			    p->gname, p->perm, true);
	}

	free_file_attr(a);
	return (EPKG_OK);
}

static int
setmod(struct plist *p, char *line, struct file_attr *a)
{
	void *set;

	p->perm = 0;

	if (line[0] == '\0')
		return (EPKG_OK);

	if ((set = setmode(line)) == NULL)
		pkg_emit_error("%s wrong mode value", line);
	else
		p->perm = getmode(set, 0);

	free_file_attr(a);

	return (EPKG_OK);
}

static int
setowner(struct plist *p, char *line, struct file_attr *a)
{
	if (line[0] == '\0')
		p->uname = NULL;
	else
		p->uname = line;

	free_file_attr(a);

	return (EPKG_OK);
}

static int
setgroup(struct plist *p, char *line, struct file_attr *a)
{
	if (line[0] == '\0')
		p->gname = NULL;
	else
		p->gname = line;

	free_file_attr(a);

	return (EPKG_OK);
}

static int
comment_key(struct plist *p, char *line, struct file_attr *a)
{
	char *name, *version, *line_options, *line_options2, *option;

	if (strncmp(line, "DEPORIGIN:", 10) == 0) {
		line += 10;
		name = p->pkgdep;
		version = strrchr(name, '-');
		version[0] = '\0';
		version++;
		pkg_adddep(p->pkg, name, line, version, false);
		p->pkgdep = NULL;
	} else if (strncmp(line, "ORIGIN:", 7) == 0) {
		line += 7;
		pkg_set(p->pkg, PKG_ORIGIN, line);
	} else if (strncmp(line, "OPTIONS:", 8) == 0) {
		line += 8;
		/* OPTIONS:+OPTION -OPTION */
		if (line[0] != '\0') {
			line_options2 = line_options = strdup(line);
			while ((option = strsep(&line_options, " ")) != NULL) {
				if ((option[0] == '+' || option[0] == '-') &&
				    option[1] != '\0' && isupper(option[1]))
					pkg_addoption(p->pkg, option + 1,
					    option[0] == '+' ? "on" : "off");
			}
			free(line_options2);
		}
	}

	/* ignore md5 will be recomputed anyway */

	free_file_attr(a);

	return (EPKG_OK);
}

static int
ignore_next(struct plist *p, __unused char *line, struct file_attr *a)
{
	p->ignore_next = true;
	free_file_attr(a);

	return (EPKG_OK);
}

static void
parse_post(struct plist *p)
{
	const char *env;
	char *token;

	if ((env = getenv("FORCE_POST")) == NULL)
		return;

	p->post_patterns = sl_init();
	p->post_pattern_to_free = strdup(env);
	while ((token = strsep(&p->post_pattern_to_free, " \t")) != NULL) {
		if (token[0] == '\0')
			continue;
		sl_add(p->post_patterns, token);
	}
}

static bool
should_be_post(char *cmd, struct plist *p)
{
	size_t i;

	if (p->post_patterns == NULL)
		parse_post(p);

	for (i = 0; i < p->post_patterns->sl_cur; i++)
		if (strstr(cmd, p->post_patterns->sl_str[i]))
			return (true);

	return (false);
}

static int
meta_exec(struct plist *p, char *line, struct file_attr *a, bool unexec)
{
	char *cmd, *buf, *tmp;
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
		if (STARTS_WITH(cmd, "rmdir ") || STARTS_WITH(cmd, "/bin/rmdir ")) {
			comment[0] = '#';
			comment[1] = '\0';

			/* remove the glob if any */
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

		if (should_be_post(cmd, p)) {
			if (comment[0] != '#')
				post_unexec_append(p->post_deinstall_buf,
				    "%s%s\n", comment, cmd);
		} else {
			pre_unexec_append(p->pre_deinstall_buf, "%s%s\n", comment, cmd);
		}
		if (comment[0] == '#') {
			buf = cmd;
			regex_t preg;

			/* remove the @dirrm{,try}
			 * command */
			while (!isspace(buf[0]))
				buf++;

			if ((tmp = strchr(buf, '|')) != NULL)
				tmp[0] = '\0';

			if (strstr(buf, "\"/")) {
				regcomp(&preg, "[[:space:]]\"(/[^\"]+)",
				    REG_EXTENDED);
				while (regexec(&preg, buf, 2, pmatch, 0) == 0) {
					strlcpy(path, &buf[pmatch[1].rm_so],
					    pmatch[1].rm_eo - pmatch[1].rm_so + 1);
					buf+=pmatch[1].rm_eo;
					if (!strcmp(path, "/dev/null"))
						continue;
					dirrmtry(p, path, a);
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
					dirrmtry(p, path, a);
				}
			}
			regfree(&preg);

		}
	} else {
		exec_append(p->post_install_buf, "%s\n", cmd);
	}
	free_file_attr(a);

	return (EPKG_OK);
}

static int
exec(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_exec(p, line, a, false));
}

static int
unexec(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_exec(p, line, a, true));
}

static struct keyact {
	const char *key;
	int (*action)(struct plist *, char *, struct file_attr *);
} keyacts[] = {
	{ "cwd", setprefix },
	{ "ignore", ignore_next },
	{ "comment", comment_key },
	{ "dirrm", dirrm },
	{ "dirrmtry", dirrmtry },
	{ "mode", setmod },
	{ "owner", setowner },
	{ "group", setgroup },
	{ "exec", exec },
	{ "unexec", unexec },
	/* old pkg compat */
	{ "name", name_key },
	{ "pkgdep", pkgdep },
	{ NULL, NULL },
};

static void
populate_keywords(struct plist *p)
{
	struct keyword *k;
	struct action *a;
	int i;

	for (i = 0; keyacts[i].key != NULL; i++) {
		k = calloc(1, sizeof(struct keyword));
		a = malloc(sizeof(struct action));
		strlcpy(k->keyword, keyacts[i].key, sizeof(k->keyword));
		a->perform = keyacts[i].action;
		LL_APPEND(k->actions, a);
		HASH_ADD_STR(p->keywords, keyword, k);
	}
}

static void
keyword_free(struct keyword *k)
{
	LL_FREE(k->actions, action, free);

	free(k);
}

static int
parse_actions(yaml_document_t *doc, yaml_node_t *node, struct plist *p,
    char *line, struct file_attr *a)
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
				list_actions[i].perform(p, line, a);
				break;
			}
		}
		++item;
	}

	return (EPKG_OK);
}

static void
parse_attributes(yaml_document_t *doc, yaml_node_t *node, struct file_attr **a) {
	yaml_node_pair_t *pair;
	yaml_node_t *key, *val;

	if (*a == NULL)
		*a = calloc(1, sizeof(struct file_attr));

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);
		if (key->data.scalar.length <= 0) {
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "owner")) {
			if (val->type == YAML_SCALAR_NODE) {
				if ((*a)->owner == NULL)
					(*a)->owner = strdup(val->data.scalar.value);
			} else {
				pkg_emit_error("Expecting a scalar for the owner attribute, ignored");
			}
			++pair;
			continue;
		}
		if (!strcasecmp(key->data.scalar.value, "group")) {
			if (val->type == YAML_SCALAR_NODE) {
				if ((*a)->group == NULL)
					(*a)->group = strdup(val->data.scalar.value);
			} else {
				pkg_emit_error("Expecting a scalar for the group attribute, ignored");
			}
		}
		if (!strcasecmp(key->data.scalar.value, "mode")) {
			if (val->type == YAML_SCALAR_NODE) {
				if ((*a)->mode == 0) {
					void *set;
					if ((set = setmode(val->data.scalar.value)) == NULL)
						pkg_emit_error("Bad format for the mode attribute: %s", val->data.scalar.value);
					else
						(*a)->mode = getmode(set, 0);
					free(set);
				}
			} else {
				pkg_emit_error("Expecting a scalar for the mode attribute, ignored");
			}
		}
		++pair;
		continue;
	}
}


static int
parse_and_apply_keyword_file(yaml_document_t *doc, yaml_node_t *node,
    struct plist *p, char *line, struct file_attr *attr)
{
	yaml_node_pair_t *pair;
	yaml_node_t *key, *val;
	yaml_node_t *actions = NULL;
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
			actions = val;
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "attributes")) {
			parse_attributes(doc, val, &attr);
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

	if (actions != NULL)
		parse_actions(doc, actions, p, line, attr);

	return (EPKG_OK);
}

static int
external_keyword(struct plist *plist, char *keyword, char *line, struct file_attr *attr)
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
			    line, attr);
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
	struct file_attr *attr = NULL;
	char *tmp;
	int ret = EPKG_FATAL;
	char *owner = NULL;
	char *group = NULL;
	char *permstr = NULL;
	void *set = NULL;

	if ((tmp = strchr(keyword, '(')) != NULL &&
	    keyword[strlen(keyword) -1] != ')') {
		pkg_emit_error("Malformed keyword %s, expecting @keyword "
		    "or @keyword(owner,group,mode)", keyword);
		return (ret);
	}

	if (tmp != NULL) {
		tmp[0] = '\0';
		tmp++;
		owner = tmp;
		if ((tmp = strchr(tmp, ',')) == NULL) {
			pkg_emit_error("Malformed keyword %s, expecting @keyword "
			    "or @keyword(owner,group,mode)", keyword);
			return (ret);
		}
		tmp[0] = '\0';
		tmp++;
		group = tmp;
		if ((tmp = strchr(tmp, ',')) == NULL) {
			pkg_emit_error("Malformed keyword %s, expecting @keyword "
			    "or @keyword(owner,group,mode)", keyword);
			return (ret);
		}
		tmp[0] = '\0';
		tmp++;
		permstr = tmp;
		if (*permstr != '\0' && (set = setmode(permstr)) == NULL) {
			pkg_emit_error("Malformed keyword %s, wrong mode section",
			    keyword);
			return (ret);
		}

		/* remove the trailing ) */
		permstr[strlen(permstr) - 1] = '\0';
		attr = calloc(1, sizeof(struct file_attr));
		if (*owner != '\0')
			attr->owner = owner;
		if (*group != '\0')
			attr->group = group;
		if (*permstr != '\0') {
			attr->mode = getmode(set, 0);
			free(set);
		}
	}

	/* if keyword is empty consider it as a file */
	if (*keyword == '\0')
		return (file(plist, line, attr));

	HASH_FIND_STR(plist->keywords, keyword, k);
	if (k != NULL) {
		LL_FOREACH(k->actions, a) {
			ret = a->perform(plist, line, attr);
			if (ret != EPKG_OK)
				return (ret);
		}
		return (ret);
	}

	/*
	 * if we are it means the keyword as not been found
	 * maybe it is defined externally
	 * let's try to find it
	 */
	return (external_keyword(plist, keyword, line, attr));
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
	char *plist_buf, *walk, *buf, *token;
	int ret = EPKG_OK;
	off_t sz = 0;
	struct plist pplist;

	assert(pkg != NULL);
	assert(plist != NULL);

	pplist.last_file = NULL;
	pplist.prefix = NULL;
	pplist.stage = stage;
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
	pplist.hardlinks = NULL;
	pplist.flatsize = 0;
	pplist.keywords = NULL;
	pplist.post_pattern_to_free = NULL;
	pplist.post_patterns = NULL;

	populate_keywords(&pplist);

	buf = NULL;

	if ((ret = file_to_buffer(plist, &plist_buf, &sz)) != EPKG_OK)
		return (ret);

	pkg_get(pkg, PKG_PREFIX, &pplist.prefix);
	if (pplist.prefix != NULL)
		pplist.slash = pplist.prefix[strlen(pplist.prefix) - 1] == '/' ? "" : "/";

	walk = plist_buf;

	while ((token = strsep(&walk, "\n")) != NULL) {
		if (pplist.ignore_next) {
			pplist.ignore_next = false;
			continue;
		}

		if (token[0] == '\0')
			continue;

		if (token[0] == '@') {
			char *keyword = token;

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
				    keyword, token);
				break;
			case EPKG_FATAL:
				ret = EPKG_FATAL;
				break;
			}
		} else {
			buf = token;
			pplist.last_file = buf;

			/* remove spaces at the begining and at the end */
			while (isspace(buf[0]))
				buf++;

			if (file(&pplist, buf, NULL) != EPKG_OK)
				ret = EPKG_FATAL;
		}
	}

	pkg_set(pkg, PKG_FLATSIZE, pplist.flatsize);

	flush_script_buffer(pplist.pre_install_buf, pkg,
	    PKG_SCRIPT_PRE_INSTALL);
	flush_script_buffer(pplist.post_install_buf, pkg,
	    PKG_SCRIPT_POST_INSTALL);
	flush_script_buffer(pplist.pre_deinstall_buf, pkg,
	    PKG_SCRIPT_PRE_DEINSTALL);
	flush_script_buffer(pplist.post_deinstall_buf, pkg,
	    PKG_SCRIPT_POST_DEINSTALL);
	flush_script_buffer(pplist.pre_upgrade_buf, pkg,
	    PKG_SCRIPT_PRE_UPGRADE);
	flush_script_buffer(pplist.post_upgrade_buf, pkg,
	    PKG_SCRIPT_POST_UPGRADE);

	HASH_FREE(pplist.hardlinks, hardlinks, free);

	free(plist_buf);
	HASH_FREE(pplist.keywords, keyword, keyword_free);

	free(pplist.post_pattern_to_free);
	if (pplist.post_patterns != NULL)
		sl_free(pplist.post_patterns, 0);

	return (ret);
}
