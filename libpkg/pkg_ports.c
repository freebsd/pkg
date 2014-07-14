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
#define _WITH_GETLINE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <uthash.h>

#include "pkg.h"
#include "private/utils.h"
#include "private/event.h"
#include "private/pkg.h"

static ucl_object_t *keyword_schema = NULL;

struct keyword {
	/* 64 is more than enough for this */
	char keyword[64];
	struct action *actions;
	UT_hash_handle hh;
};

struct plist {
	char last_file[MAXPATHLEN];
	const char *stage;
	char prefix[MAXPATHLEN];
	struct sbuf *pre_install_buf;
	struct sbuf *post_install_buf;
	struct sbuf *pre_deinstall_buf;
	struct sbuf *post_deinstall_buf;
	struct sbuf *pre_upgrade_buf;
	struct sbuf *post_upgrade_buf;
	struct pkg *pkg;
	char *uname;
	char *gname;
	const char *slash;
	char *pkgdep;
	bool ignore_next;
	int64_t flatsize;
	struct hardlinks *hardlinks;
	mode_t perm;
	struct {
		char *buf;
		char **patterns;
		size_t len;
		size_t cap;
	} post_patterns;
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

static ucl_object_t *
keyword_open_schema(void)
{
	struct ucl_parser *parser;
	static const char keyword_schema_str[] = ""
		"{"
		"  type = object;"
		"  properties {"
		"    actions = { "
		"      type = array; "
		"      items = { type = string }; "
		"      uniqueItems: true "
		"    }; "
		"    attributes = { "
		"      type = object; "
		"      properties { "
		"        owner = { type = string }; "
		"        group = { type = string }; "
		"        mode = { oneOf: [ { type = integer }, { type = string } ] }; "
		"      }"
		"    }; "
		"    pre-install = { type = string }; "
		"    post-install = { type = string }; "
		"    pre-deinstall = { type = string }; "
		"    post-deinstall = { type = string }; "
		"    pre-upgrade = { type = string }; "
		"    post-upgrade = { type = string }; "
		"  }"
		"}";

	if (keyword_schema != NULL)
		return (keyword_schema);

	parser = ucl_parser_new(0);
	if (!ucl_parser_add_chunk(parser, keyword_schema_str,
	    sizeof(keyword_schema_str) -1)) {
		pkg_emit_error("Cannot parse schema for keywords: %s",
		    ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return (NULL);
	}

	keyword_schema = ucl_parser_get_object(parser);
	ucl_parser_free(parser);

	return (keyword_schema);
}

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
	if (line[0] == '\0') {
		pkg_get(p->pkg, PKG_PREFIX, &pkgprefix);
		strlcpy(p->prefix, pkgprefix, sizeof(p->prefix));
	}
	else
		strlcpy(p->prefix, line, sizeof(p->prefix));

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
	if (*line != '\0') {
		if (p->pkgdep != NULL) {
			free(p->pkgdep);
		}
		p->pkgdep = strdup(line);
	}
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
	int ret = EPKG_OK;

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
		pkg_emit_errno("lstat", testpath);
		if (p->stage != NULL)
			ret = EPKG_FATAL;
		developer = pkg_object_bool(pkg_config_get("DEVELOPER_MODE"));
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
	int ret = EPKG_OK;

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
		pkg_emit_errno("lstat", testpath);
		if (p->stage != NULL)
			ret = EPKG_FATAL;
		developer = pkg_object_bool(pkg_config_get("DEVELOPER_MODE"));
		if (developer) {
			pkg_emit_developer_mode("Plist error, missing file: %s", line);
			ret = EPKG_FATAL;
		}
	} else {
		buf = NULL;
		regular = false;

		if (S_ISDIR(st.st_mode)) {
			pkg_emit_error("Plist error, directory listed as a file: %s", line);
			free_file_attr(a);
			return (EPKG_FATAL);
		} else if (S_ISREG(st.st_mode)) {
			if (st.st_nlink > 1)
				regular = !check_for_hardlink(p->hardlinks, &st);
			else
				regular = true;

		} else if (S_ISLNK(st.st_mode)) {
			if (pkg_symlink_cksum(testpath, p->stage, sha256) == EPKG_OK) {
				buf = sha256;
				regular = false;
			}
			else
				return (EPKG_FATAL);
		}

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
	return (ret);
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
	if (line[0] == '\0') {
		if (p->uname != NULL)
			free(p->uname);
		p->uname = NULL;
	}
	else {
		if (p->uname != NULL)
			free(p->uname);
		p->uname = strdup(line);
	}

	free_file_attr(a);

	return (EPKG_OK);
}

static int
setgroup(struct plist *p, char *line, struct file_attr *a)
{
	if (line[0] == '\0')
		p->gname = NULL;
	else
		p->gname = strdup(line);

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
		if (name != NULL) {
			version = strrchr(name, '-');
			version[0] = '\0';
			version++;
			pkg_adddep(p->pkg, name, line, version, false);
			free(p->pkgdep);
		}
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

	p->post_patterns.buf = strdup(env);
	while ((token = strsep(&p->post_patterns.buf, " \t")) != NULL) {
		if (token[0] == '\0')
			continue;
		if (p->post_patterns.len >= p->post_patterns.cap) {
			p->post_patterns.cap += 10;
			p->post_patterns.patterns = reallocf(p->post_patterns.patterns, p->post_patterns.cap * sizeof (char *));
		}
		p->post_patterns.patterns[p->post_patterns.len++] = token;
	}
}

static bool
should_be_post(char *cmd, struct plist *p)
{
	size_t i;

	if (p->post_patterns.patterns == NULL)
		parse_post(p);

	if (p->post_patterns.patterns == NULL)
		return (false);

	for (i = 0; i < p->post_patterns.len ; i++)
		if (strstr(cmd, p->post_patterns.patterns[i]))
			return (true);

	return (false);
}

static int
meta_exec(struct plist *p, char *line, struct file_attr *a, bool unexec)
{
	char *cmd, *buf, *tmp;
	char comment[2];
	char path[MAXPATHLEN];
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
					a = NULL;
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
					a = NULL;
				}
			}
			regfree(&preg);

		}
	} else {
		exec_append(p->post_install_buf, "%s\n", cmd);
	}
	free_file_attr(a);
	free(cmd);

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
	{ "mtree", comment_key },
	{ "stopdaemon", comment_key },
	{ "display", comment_key },
	{ "conflicts", comment_key },
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
	LL_FREE(k->actions, free);

	free(k);
}

static int
parse_actions(const ucl_object_t *o, struct plist *p,
    char *line, struct file_attr *a)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	int i;

	while ((cur = ucl_iterate_object(o, &it, true))) {
		for (i = 0; list_actions[i].name != NULL; i++) {
			if (!strcasecmp(ucl_object_tostring(cur), list_actions[i].name)) {
				list_actions[i].perform(p, line, a);
				break;
			}
		}
	}

	return (EPKG_OK);
}

static void
parse_attributes(const ucl_object_t *o, struct file_attr **a) {
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *key;

	if (*a == NULL)
		*a = calloc(1, sizeof(struct file_attr));

	while ((cur = ucl_iterate_object(o, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;
		if (!strcasecmp(key, "owner") && cur->type == UCL_STRING) {
			free((*a)->owner);
			(*a)->owner = strdup(ucl_object_tostring(cur));
			continue;
		}
		if (!strcasecmp(key, "group") && cur->type == UCL_STRING) {
			free((*a)->group);
			(*a)->group = strdup(ucl_object_tostring(cur));
			continue;
		}
		if (!strcasecmp(key, "mode")) {
			if (cur->type == UCL_STRING) {
				void *set;
				if ((set = setmode(ucl_object_tostring(cur))) == NULL)
					pkg_emit_error("Bad format for the mode attribute: %s", ucl_object_tostring(cur));
				else
					(*a)->mode = getmode(set, 0);
				free(set);
			} else {
				pkg_emit_error("Expecting a string for the mode attribute, ignored");
			}
		}
	}
}

static int
apply_keyword_file(ucl_object_t *obj, struct plist *p, char *line, struct file_attr *attr)
{
	const ucl_object_t *o;
	char *cmd;

	if ((o = ucl_object_find_key(obj,  "attributes")))
		parse_attributes(o, &attr);

	if ((o = ucl_object_find_key(obj, "pre-install"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix, p->last_file, line);
		sbuf_printf(p->pre_install_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "post-install"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix, p->last_file, line);
		sbuf_printf(p->post_install_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "pre-deinstall"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix, p->last_file, line);
		sbuf_printf(p->pre_deinstall_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "post-deinstall"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix, p->last_file, line);
		sbuf_printf(p->post_deinstall_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "pre-upgrade"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix, p->last_file, line);
		sbuf_printf(p->pre_deinstall_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "post-upgrade"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix, p->last_file, line);
		sbuf_printf(p->post_deinstall_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj,  "actions")))
		parse_actions(o, p, line, attr);

	return (EPKG_OK);
}

static ucl_object_t *
external_yaml_keyword(char *keyword)
{
	const char *keyword_dir = NULL;
	char keyfile_path[MAXPATHLEN];

	keyword_dir = pkg_object_string(pkg_config_get("PLIST_KEYWORDS_DIR"));
	if (keyword_dir == NULL) {
		keyword_dir = pkg_object_string(pkg_config_get("PORTSDIR"));
		snprintf(keyfile_path, sizeof(keyfile_path),
		    "%s/Keywords/%s.yaml", keyword_dir, keyword);
	} else {
		snprintf(keyfile_path, sizeof(keyfile_path),
		    "%s/%s.yaml", keyword_dir, keyword);
	}

	return (yaml_to_ucl(keyfile_path, NULL, 0));
}

static int
external_keyword(struct plist *plist, char *keyword, char *line, struct file_attr *attr)
{
	struct ucl_parser *parser;
	const char *keyword_dir = NULL;
	char keyfile_path[MAXPATHLEN];
	int ret = EPKG_UNKNOWN;
	ucl_object_t *o, *schema;
	struct ucl_schema_error err;

	keyword_dir = pkg_object_string(pkg_config_get("PLIST_KEYWORDS_DIR"));
	if (keyword_dir == NULL) {
		keyword_dir = pkg_object_string(pkg_config_get("PORTSDIR"));
		snprintf(keyfile_path, sizeof(keyfile_path),
		    "%s/Keywords/%s.ucl", keyword_dir, keyword);
	} else {
		snprintf(keyfile_path, sizeof(keyfile_path),
		    "%s/%s.ucl", keyword_dir, keyword);
	}

	if (eaccess(keyfile_path, R_OK) != 0) {
		if ((o = external_yaml_keyword(keyword)) == NULL)
			return (EPKG_UNKNOWN);
	} else {
		parser = ucl_parser_new(0);
		if (!ucl_parser_add_file(parser, keyfile_path)) {
			pkg_emit_error("cannot parse keyword: %s",
			    ucl_parser_get_error(parser));
			ucl_parser_free(parser);
			return (EPKG_UNKNOWN);
		}

		o = ucl_parser_get_object(parser);
		ucl_parser_free(parser);
	}

	schema = keyword_open_schema();

	if (schema != NULL) {
		if (!ucl_object_validate(schema, o, &err)) {
			pkg_emit_error("Keyword definition %s cannot be validated: %s", keyfile_path, err.msg);
			ucl_object_unref(o);
			return (EPKG_FATAL);
		}
	}

	ret = apply_keyword_file(o, plist, line, attr);

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
ports_parse_plist(struct pkg *pkg, const char *plist, const char *stage)
{
	char *buf, *line = NULL, *tmpprefix;
	int ret = EPKG_OK;
	struct plist pplist;
	FILE *plist_f;
	size_t linecap = 0;
	ssize_t linelen;

	assert(pkg != NULL);
	assert(plist != NULL);

	pplist.last_file[0] = '\0';
	pplist.prefix[0] = '\0';
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
	pplist.post_patterns.buf = NULL;
	pplist.post_patterns.patterns = NULL;
	pplist.post_patterns.cap = 0;
	pplist.post_patterns.len = 0;
	pplist.pkgdep = NULL;

	populate_keywords(&pplist);

	buf = NULL;

	if ((plist_f = fopen(plist, "r")) == NULL) {
		pkg_emit_error("Unable to open plist file: %s", plist);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_PREFIX, &tmpprefix);
	if (tmpprefix) {
		strlcpy(pplist.prefix, tmpprefix, sizeof(pplist.prefix));
		pplist.slash = pplist.prefix[strlen(pplist.prefix) - 1] == '/' ? "" : "/";
	}

	while ((linelen = getline(&line, &linecap, plist_f)) > 0) {
		if (line[linelen - 1] == '\n')
			line[linelen - 1] = '\0';

		if (pplist.ignore_next) {
			pplist.ignore_next = false;
			continue;
		}

		if (line[0] == '\0')
			continue;

		pkg_debug(1, "Parsing plist line: '%s'", line);

		if (line[0] == '@') {
			char *keyword = line;

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
			pkg_debug(1, "Parsing plist, found keyword: '%s", keyword);

			switch (parse_keywords(&pplist, keyword, buf)) {
			case EPKG_UNKNOWN:
				pkg_emit_error("unknown keyword %s, ignoring %s",
				    keyword, line);
				break;
			case EPKG_FATAL:
				ret = EPKG_FATAL;
				break;
			}
		} else {
			buf = line;
			strlcpy(pplist.last_file, buf, sizeof(pplist.last_file));

			/* remove spaces at the begining and at the end */
			while (isspace(buf[0]))
				buf++;

			if (file(&pplist, buf, NULL) != EPKG_OK)
				ret = EPKG_FATAL;
		}
	}

	free(line);

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

	HASH_FREE(pplist.hardlinks, free);

	HASH_FREE(pplist.keywords, keyword_free);

	if (pplist.pkgdep != NULL)
		free(pplist.pkgdep);
	if (pplist.uname != NULL)
		free(pplist.uname);
	if (pplist.gname != NULL)
		free(pplist.gname);
	free(pplist.post_patterns.buf);
	free(pplist.post_patterns.patterns);

	fclose(plist_f);

	return (ret);
}
