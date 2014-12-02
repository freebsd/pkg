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

static int setprefix(struct plist *, char *, struct file_attr *);
static int dir(struct plist *, char *, struct file_attr *);
static int dirrm(struct plist *, char *, struct file_attr *);
static int file(struct plist *, char *, struct file_attr *);
static int setmod(struct plist *, char *, struct file_attr *);
static int setowner(struct plist *, char *, struct file_attr *);
static int setgroup(struct plist *, char *, struct file_attr *);
static int ignore_next(struct plist *, char *, struct file_attr *);
static int comment_key(struct plist *, char *, struct file_attr *);
static int config(struct plist *, char *, struct file_attr *);
/* compat with old packages */
static int name_key(struct plist *, char *, struct file_attr *);
static int pkgdep(struct plist *, char *, struct file_attr *);

static struct action_cmd {
	const char *name;
	int (*perform)(struct plist *, char *, struct file_attr *);
	size_t namelen;
} list_actions[] = {
	{ "setprefix", setprefix, 9},
	{ "dirrm", dirrm, 5 },
	{ "dirrmtry", dirrm, 7 },
	{ "dir", dir, 3 },
	{ "file", file, 4 },
	{ "setmode", setmod, 6 },
	{ "setowner", setowner, 8 },
	{ "setgroup", setgroup, 8 },
	{ "comment", comment_key, 7 },
	{ "ignore_next", ignore_next, 11 },
	{ "config", config, 6 },
	/* compat with old packages */
	{ "name", name_key, 4 },
	{ "pkgdep", pkgdep, 6 },
	{ NULL, NULL, 0 }
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

void *
parse_mode(const char *str)
{
	if (str == NULL || *str == '\0')
		return (NULL);

	if (strstr(str, "u+") || strstr(str, "o+") || strstr(str, "g+") ||
	    strstr(str, "u-") || strstr(str, "o-") || strstr(str, "g-") ||
	    strstr(str, "a+") || strstr(str, "a-"))
		return (NULL);

	return (setmode(str));
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
	/* if no arguments then set default prefix */
	if (line[0] == '\0') {
		strlcpy(p->prefix, p->pkg->prefix, sizeof(p->prefix));
	}
	else
		strlcpy(p->prefix, line, sizeof(p->prefix));

	if (p->pkg->prefix == NULL)
		p->pkg->prefix = strdup(line);

	p->slash = p->prefix[strlen(p->prefix) -1] == '/' ? "" : "/";

	exec_append(p->post_install_buf, "cd %s\n", p->prefix);
	pre_unexec_append(p->pre_deinstall_buf, "cd %s\n", p->prefix);
	post_unexec_append(p->post_deinstall_buf, "cd %s\n", p->prefix);

	free_file_attr(a);

	return (EPKG_OK);
}

static int
name_key(struct plist *p, char *line, struct file_attr *a)
{
	char *tmp;

	if (p->pkg->name != NULL) {
		free_file_attr(a);

		return (EPKG_OK);
	}
	tmp = strrchr(line, '-');
	tmp[0] = '\0';
	tmp++;
	p->pkg->name = strdup(line);
	p->pkg->version = strdup(tmp);

	free_file_attr(a);

	return (EPKG_OK);
}

static int
pkgdep(struct plist *p, char *line, struct file_attr *a)
{
	if (*line != '\0') {
		free(p->pkgdep);
		p->pkgdep = strdup(line);
	}
	free_file_attr(a);

	return (EPKG_OK);
}

static int
dir(struct plist *p, char *line, struct file_attr *a)
{
	size_t len;
	char path[MAXPATHLEN];
	char stagedpath[MAXPATHLEN];
	char *testpath;
	struct stat st;
	int ret = EPKG_OK;

	len = strlen(line);

	while (isspace(line[len - 1]))
		line[--len] = '\0';

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
		if (developer_mode) {
			pkg_emit_developer_mode("Plist error: @dirrm %s", line);
			ret = EPKG_FATAL;
		}
	} else {
		if (a != NULL)
			ret = pkg_adddir_attr(p->pkg, path,
			    a->owner ? a->owner : p->uname,
			    a->group ? a->group : p->gname,
			    a->mode ? a->mode : p->perm,
			    true, true);
		else
			ret = pkg_adddir_attr(p->pkg, path, p->uname, p->gname,
			    p->perm, true, true);
	}

	free_file_attr(a);
	return (ret);
}

static void
warn_deprecated_dir(void)
{
	static bool warned_deprecated_dir = false;

	if (warned_deprecated_dir)
		return;
	warned_deprecated_dir = true;

	if (developer_mode)
		pkg_emit_error("Warning: @dirrm[try] is deprecated, please"
		    " use @dir");
}

static int
dirrm(struct plist *p, char *line, struct file_attr *a)
{

	warn_deprecated_dir();
	return (dir(p, line, a));
}

static int
meta_file(struct plist *p, char *line, struct file_attr *a, bool is_config)
{
	size_t len;
	char path[MAXPATHLEN];
	char stagedpath[MAXPATHLEN];
	char *testpath;
	struct stat st;
	char *buf;
	bool regular = false;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	int ret = EPKG_OK;

	len = strlen(line);

	while (isspace(line[len - 1]))
		line[--len] = '\0';

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
		pkg_emit_error("Unable to access file %s: %s", testpath,
		    strerror(errno));
		if (p->stage != NULL)
			ret = EPKG_FATAL;
		if (developer_mode) {
			pkg_emit_developer_mode("Plist error, missing file: %s",
			    line);
			ret = EPKG_FATAL;
		}
		free_file_attr(a);
		return (ret);
	}
	buf = NULL;
	regular = false;

	if (S_ISREG(st.st_mode)) {
		if (st.st_nlink > 1)
			regular = !check_for_hardlink(&(p->hardlinks), &st);
		else
			regular = true;
	} else if (S_ISLNK(st.st_mode)) {
		if (pkg_symlink_cksum(testpath, p->stage, sha256) == EPKG_OK) {
			buf = sha256;
			regular = false;
		} else {
			free_file_attr(a);
			return (EPKG_FATAL);
		}
	}

	if (regular) {
		p->flatsize += st.st_size;
		sha256_file(testpath, sha256);
		buf = sha256;
		if (is_config) {
			size_t sz;
			char *content;
			file_to_buffer(testpath, &content, &sz);
			pkg_addconfig_file(p->pkg, path, content);
			free(content);
		}
	} else {
		if (is_config) {
			pkg_emit_error("Plist error, @config %s: not a regular "
			    "file", line);
			free_file_attr(a);
			return (EPKG_FATAL);
		}
	}

	if (S_ISDIR(st.st_mode) &&
	    !pkg_object_bool(pkg_config_get("PLIST_ACCEPT_DIRECTORIES"))) {
		pkg_emit_error("Plist error, directory listed as a file: %s",
		    line);
		free_file_attr(a);
		return (EPKG_FATAL);
	}

	if (S_ISDIR(st.st_mode)) {
		if (a != NULL)
			ret = pkg_adddir_attr(p->pkg, path,
			    a->owner ? a->owner : p->uname,
			    a->group ? a->group : p->gname,
			    a->mode ? a->mode : p->perm,
			    true, true);
		else
			ret = pkg_adddir_attr(p->pkg, path, p->uname, p->gname,
			    p->perm, true, true);
	} else {
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
config(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_file(p, line, a, true));
}

static int
file(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_file(p, line, a, false));
}

static int
setmod(struct plist *p, char *line, struct file_attr *a)
{
	void *set;

	p->perm = 0;

	if (line[0] == '\0')
		return (EPKG_OK);

	if ((set = parse_mode(line)) == NULL) {
		pkg_emit_error("%s wrong mode value", line);
		return (EPKG_FATAL);
	}
	p->perm = getmode(set, 0);

	free_file_attr(a);

	return (EPKG_OK);
}

static int
setowner(struct plist *p, char *line, struct file_attr *a)
{
	free(p->uname);
	if (line[0] == '\0')
		p->uname = strdup("root");
	else
		p->uname = strdup(line);

	free_file_attr(a);

	return (EPKG_OK);
}

static int
setgroup(struct plist *p, char *line, struct file_attr *a)
{
	free(p->gname);
	if (line[0] == '\0')
		p->gname = strdup("wheel");
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
		free(p->pkg->origin);
		p->pkg->origin = strdup(line);
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

typedef enum {
	EXEC = 0,
	UNEXEC,
	PREEXEC,
	POSTEXEC,
	PREUNEXEC,
	POSTUNEXEC
} exec_t;

static int
meta_exec(struct plist *p, char *line, struct file_attr *a, exec_t type)
{
	char *cmd, *buf, *tmp;
	char comment[2];
	char path[MAXPATHLEN];
	regmatch_t pmatch[2];
	int ret;

	ret = format_exec_cmd(&cmd, line, p->prefix, p->last_file, NULL, 0,
	    NULL);
	if (ret != EPKG_OK)
		return (EPKG_OK);

	switch (type) {
	case PREEXEC:
		sbuf_printf(p->pre_install_buf, "%s\n", cmd);
		break;
	case POSTEXEC:
		sbuf_printf(p->post_install_buf, "%s\n", cmd);
		break;
	case PREUNEXEC:
		sbuf_printf(p->pre_deinstall_buf, "%s\n", cmd);
		break;
	case POSTUNEXEC:
		sbuf_printf(p->post_deinstall_buf, "%s\n", cmd);
		break;
	case UNEXEC:
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
					dir(p, path, a);
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
					dir(p, path, a);
					a = NULL;
				}
			}
			regfree(&preg);

		}
		break;
	case EXEC:
		exec_append(p->post_install_buf, "%s\n", cmd);
		break;
	}

	free_file_attr(a);
	free(cmd);

	return (EPKG_OK);
}

static int
preunexec(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_exec(p, line, a, PREUNEXEC));
}

static int
postunexec(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_exec(p, line, a, POSTUNEXEC));
}

static int
preexec(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_exec(p, line, a, PREEXEC));
}

static int
postexec(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_exec(p, line, a, POSTEXEC));
}

static int
exec(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_exec(p, line, a, EXEC));
}

static int
unexec(struct plist *p, char *line, struct file_attr *a)
{
	return (meta_exec(p, line, a, UNEXEC));
}

static struct keyact {
	const char *key;
	int (*action)(struct plist *, char *, struct file_attr *);
} keyacts[] = {
	{ "cwd", setprefix },
	{ "ignore", ignore_next },
	{ "comment", comment_key },
	{ "config", config },
	{ "dir", dir },
	{ "dirrm", dirrm },
	{ "dirrmtry", dirrm },
	{ "mode", setmod },
	{ "owner", setowner },
	{ "group", setgroup },
	{ "exec", exec },
	{ "unexec", unexec },
	{ "preexec", preexec },
	{ "postexec", postexec },
	{ "preunexec", preunexec },
	{ "postunexec", postunexec },
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
    char *line, struct file_attr *a, int argc, char **argv)
{
	const ucl_object_t *cur;
	const char *actname;
	ucl_object_iter_t it = NULL;
	int i, j = 0;

	while ((cur = ucl_iterate_object(o, &it, true))) {
		actname = ucl_object_tostring(cur);
		for (i = 0; list_actions[i].name != NULL; i++) {
			if (!strncasecmp(actname, list_actions[i].name,
			    list_actions[i].namelen) &&
			    (actname[list_actions[i].namelen ] == '\0' ||
			     actname[list_actions[i].namelen ] == '(' )) {
				actname += list_actions[i].namelen;
				if (*actname == '(') {
					j = strtol(actname+1, NULL, 10);
					if (j > argc) {
						pkg_emit_error(
						    "Invalid argument requested %d"
						    " available: %d", j, argc);
						return (EPKG_FATAL);
					}
				}
				list_actions[i].perform(p, j > 0 ? argv[j - 1] : line, a);
				break;
			}
		}
	}

	return (EPKG_OK);
}

static void
parse_attributes(const ucl_object_t *o, struct file_attr **a)
{
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
				if ((set = parse_mode(ucl_object_tostring(cur))) == NULL) {
					pkg_emit_error("Bad format for the mode attribute: %s", ucl_object_tostring(cur));
					return;
				}
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
	char **args = NULL;
	char *buf, *tofree = NULL;
	int spaces, argc = 0;

	if ((o = ucl_object_find_key(obj,  "arguments")) && ucl_object_toboolean(o)) {
		spaces = pkg_utils_count_spaces(line);
		args = malloc((spaces + 1)* sizeof(char *));
		tofree = buf = strdup(line);
		while (buf != NULL) {
			args[argc++] = pkg_utils_tokenize(&buf);
		}
	}

	if ((o = ucl_object_find_key(obj,  "attributes")))
		parse_attributes(o, &attr);

	if ((o = ucl_object_find_key(obj, "pre-install"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix,
		    p->last_file, line, argc, args);
		sbuf_printf(p->pre_install_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "post-install"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix,
		    p->last_file, line, argc, args);
		sbuf_printf(p->post_install_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "pre-deinstall"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix,
		    p->last_file, line, argc, args);
		sbuf_printf(p->pre_deinstall_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "post-deinstall"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix,
		    p->last_file, line, argc, args);
		sbuf_printf(p->post_deinstall_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "pre-upgrade"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix,
		    p->last_file, line, argc, args);
		sbuf_printf(p->pre_deinstall_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj, "post-upgrade"))) {
		format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix,
		    p->last_file, line, argc, args);
		sbuf_printf(p->post_deinstall_buf, "%s\n", cmd);
		free(cmd);
	}

	if ((o = ucl_object_find_key(obj,  "actions")))
		parse_actions(o, p, line, attr, argc, args);

	free(args);
	free(tofree);

	return (EPKG_OK);
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

	parser = ucl_parser_new(0);
	if (!ucl_parser_add_file(parser, keyfile_path)) {
		pkg_emit_error("cannot parse keyword: %s",
				ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return (EPKG_UNKNOWN);
	}

	o = ucl_parser_get_object(parser);
	ucl_parser_free(parser);

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
		tmp[strlen(tmp) -1] = '\0';
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
		if (*permstr != '\0' && ((set = parse_mode(permstr)) == NULL)) {
			pkg_emit_error("Malformed keyword %s, wrong mode section",
			    keyword);
			return (ret);
		}

		/* remove the trailing ) */
		permstr[strlen(permstr) - 1] = '\0';
		attr = calloc(1, sizeof(struct file_attr));
		if (*owner != '\0')
			attr->owner = strdup(owner);
		if (*group != '\0')
			attr->group = strdup(group);
		if (set != NULL) {
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
}

int
plist_parse_line(struct pkg *pkg, struct plist *plist, char *line)
{
	char *keyword, *buf;

	if (plist->ignore_next) {
		plist->ignore_next = false;
		return (EPKG_OK);
	}

	if (line[0] == '\0')
		return (EPKG_OK);

	pkg_debug(1, "Parsing plist line: '%s'", line);

	if (line[0] == '@') {
		keyword = line;
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

		switch (parse_keywords(plist, keyword, buf)) {
		case EPKG_UNKNOWN:
			pkg_emit_error("unknown keyword %s: %s",
			    keyword, line);
		case EPKG_FATAL:
			return (EPKG_FATAL);
		}
	} else {
		buf = line;
		strlcpy(plist->last_file, buf, sizeof(plist->last_file));

		/* remove spaces at the begining and at the end */
		while (isspace(buf[0]))
			buf++;

		if (file(plist, buf, NULL) != EPKG_OK)
			return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

struct plist *
plist_new(struct pkg *pkg, const char *stage)
{
	struct plist *p;

	p = calloc(1, sizeof(struct plist));
	if (p == NULL)
		return (NULL);

	p->pkg = pkg;
	if (pkg->prefix != NULL)
		strlcpy(p->prefix, pkg->prefix, sizeof(p->prefix));
	p->slash = p->prefix[strlen(p->prefix) - 1] == '/' ? "" : "/";
	p->stage = stage;
	p->uname = strdup("root");
	p->gname = strdup("wheel");

	p->pre_install_buf = sbuf_new_auto();
	p->post_install_buf = sbuf_new_auto();
	p->pre_deinstall_buf = sbuf_new_auto();
	p->post_deinstall_buf = sbuf_new_auto();
	p->pre_upgrade_buf = sbuf_new_auto();
	p->post_upgrade_buf = sbuf_new_auto();

	populate_keywords(p);

	return (p);
}

void
plist_free(struct plist *p)
{
	if (p == NULL)
		return;

	HASH_FREE(p->hardlinks, free);

	HASH_FREE(p->keywords, keyword_free);

	free(p->pkgdep);
	free(p->uname);
	free(p->gname);
	free(p->post_patterns.buf);
	free(p->post_patterns.patterns);

	sbuf_delete(p->post_deinstall_buf);
	sbuf_delete(p->post_install_buf);
	sbuf_delete(p->post_upgrade_buf);
	sbuf_delete(p->pre_deinstall_buf);
	sbuf_delete(p->pre_install_buf);
	sbuf_delete(p->pre_upgrade_buf);

	free(p);
}

int
ports_parse_plist(struct pkg *pkg, const char *plist, const char *stage)
{
	char *line = NULL;
	int ret, rc = EPKG_OK;
	struct plist *pplist;
	FILE *plist_f;
	size_t linecap = 0;
	ssize_t linelen;

	assert(pkg != NULL);
	assert(plist != NULL);

	if ((pplist = plist_new(pkg, stage)) == NULL)
		return (EPKG_FATAL);

	if ((plist_f = fopen(plist, "r")) == NULL) {
		pkg_emit_error("Unable to open plist file: %s", plist);
		return (EPKG_FATAL);
	}

	while ((linelen = getline(&line, &linecap, plist_f)) > 0) {
		if (line[linelen - 1] == '\n')
			line[linelen - 1] = '\0';
		ret = plist_parse_line(pkg, pplist, line);
		if (rc == EPKG_OK)
			rc = ret;
	}

	free(line);

	pkg->flatsize = pplist->flatsize;

	flush_script_buffer(pplist->pre_install_buf, pkg,
	    PKG_SCRIPT_PRE_INSTALL);
	flush_script_buffer(pplist->post_install_buf, pkg,
	    PKG_SCRIPT_POST_INSTALL);
	flush_script_buffer(pplist->pre_deinstall_buf, pkg,
	    PKG_SCRIPT_PRE_DEINSTALL);
	flush_script_buffer(pplist->post_deinstall_buf, pkg,
	    PKG_SCRIPT_POST_DEINSTALL);
	flush_script_buffer(pplist->pre_upgrade_buf, pkg,
	    PKG_SCRIPT_PRE_UPGRADE);
	flush_script_buffer(pplist->post_upgrade_buf, pkg,
	    PKG_SCRIPT_POST_UPGRADE);

	fclose(plist_f);

	plist_free(pplist);

	return (rc);
}

int
pkg_add_port(struct pkgdb *db, struct pkg *pkg, const char *input_path,
    const char *location, bool testing)
{
	int rc = EPKG_OK;

	if (location != NULL)
		pkg_kv_add(&pkg->annotations, "relocated", location, "annotation");

	pkg_emit_install_begin(pkg);

	rc = pkgdb_register_pkg(db, pkg, 0, 0);

	if (rc != EPKG_OK)
		goto cleanup;

	if (!testing) {
		/* Execute pre-install scripts */
		pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL);

		if (input_path != NULL)
			pkg_copy_tree(pkg, input_path, \
			    location ? location : "/");

		/* Execute post-install scripts */
		pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL);
	}

	if (rc == EPKG_OK)
		pkg_emit_install_finished(pkg);

cleanup:
	pkgdb_register_finale(db, rc);

	return (rc);
}
