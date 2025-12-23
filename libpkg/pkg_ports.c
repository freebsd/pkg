/*-
 * Copyright (c) 2011-2020 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include "pkg_config.h"

#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>

#include "pkg.h"
#include "private/utils.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/lua.h"

static ucl_object_t *keyword_schema = NULL;

static int override_prefix(struct plist *, char *, struct file_attr *);
static int setprefix(struct plist *, char *, struct file_attr *);
static int dir(struct plist *, char *, struct file_attr *);
static int file(struct plist *, char *, struct file_attr *);
static int setmod(struct plist *, char *, struct file_attr *);
static int setowner(struct plist *, char *, struct file_attr *);
static int setgroup(struct plist *, char *, struct file_attr *);
static int comment_key(struct plist *, char *, struct file_attr *);
static int config(struct plist *, char *, struct file_attr *);
/* compat with old packages */
static int name_key(struct plist *, char *, struct file_attr *);
static int include_plist(struct plist *, char *, struct file_attr *);
static int add_variable(struct plist *, char *, struct file_attr *);

static struct action_cmd {
	const char *name;
	int (*perform)(struct plist *, char *, struct file_attr *);
	size_t namelen;
} list_actions[] = {
	{ "setprefix", setprefix, 9},
	{ "dir", dir, 3 },
	{ "file", file, 4 },
	{ "setmode", setmod, 6 },
	{ "setowner", setowner, 8 },
	{ "setgroup", setgroup, 8 },
	{ "comment", comment_key, 7 },
	{ "config", config, 6 },
	{ "override_prefix", override_prefix, 15 },
	/* compat with old packages */
	{ "name", name_key, 4 },
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
		"    actions_script = { type = string }; "
		"    arguments = { type = boolean }; "
		"    preformat_arguments { type = boolean }; "
		"    prepackaging = { type = string }; "
		"    deprecated = { type = boolean }; "
		"    deprecation_message = { type = string }; "
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
		"    pre-install-lua = { type = string }; "
		"    post-install-lua = { type = string }; "
		"    pre-deinstall-lua = { type = string }; "
		"    post-deinstall-lua = { type = string }; "
		"    messages: {"
		"        type = array; "
		"        items = {"
		"            type = object;"
		"            properties {"
		"                message = { type = string };"
		"                type = { enum = [ upgrade, remove, install ] };"
		"            };"
		"            required [ message ];"
		"        };"
		"    };"
		"  }"
		"}";

	if (keyword_schema != NULL)
		return (keyword_schema);

	parser = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
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

void
free_file_attr(struct file_attr *a)
{
	if (a == NULL)
		return;
	free(a->owner);
	free(a->group);
	free(a);
}

static int
override_prefix(struct plist *p, char *line, struct file_attr *a __unused)
{
	char *np = NULL;

	if (line[0] != '\0')
		np = xstrdup(line);
	free(p->pkg->oprefix);
	p->pkg->oprefix = np;
	return (EPKG_OK);
}

static int
setprefix(struct plist *p, char *line, struct file_attr *a __unused)
{
	/* if no arguments then set default prefix */
	if (line[0] == '\0') {
		strlcpy(p->prefix, p->pkg->prefix, sizeof(p->prefix));
	}
	else
		strlcpy(p->prefix, line, sizeof(p->prefix));

	if (p->pkg->prefix == NULL)
		p->pkg->prefix = xstrdup(line);

	p->slash = p->prefix[strlen(p->prefix) -1] == '/' ? "" : "/";

	fprintf(p->post_install_buf->fp, "cd %s\n", p->prefix);
	fprintf(p->pre_deinstall_buf->fp, "cd %s\n", p->prefix);
	fprintf(p->post_deinstall_buf->fp, "cd %s\n", p->prefix);

	return (EPKG_OK);
}

static int
name_key(struct plist *p, char *line, struct file_attr *a __unused)
{
	char *tmp;

	if (p->pkg->name != NULL) {
		return (EPKG_OK);
	}
	tmp = strrchr(line, '-');
	if (tmp == NULL) {
		pkg_emit_error("Invalid @name key: '%s' expecting <name>-<version>", line);
		return (EPKG_FATAL);
	}
	tmp[0] = '\0';
	tmp++;
	p->pkg->name = xstrdup(line);
	free(p->pkg->uid);
	p->pkg->uid = xstrdup(line);
	p->pkg->version = xstrdup(tmp);

	return (EPKG_OK);
}

static int
lua_meta(lua_State *L,
    int (*perform)(struct plist *, char *, struct file_attr *))
{
	int n = lua_gettop(L);
	int ret;
	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "takes exactly one argument");
	char *str = strdup(luaL_checkstring(L, 1));
	lua_getglobal(L, "plist");
	struct plist *p = lua_touserdata(L, -1);
	lua_getglobal(L, "attrs");
	struct file_attr *a = lua_touserdata(L, -1);

	ret = perform(p, str, a);
	free(str);
	lua_pushboolean(L, ret == EPKG_OK);
	return (1);
}

static int
lua_dir(lua_State *L)
{
	return (lua_meta(L, dir));
}

static int
lua_config(lua_State *L) {
	return (lua_meta(L, config));
}

static int
lua_file(lua_State *L) {
	return (lua_meta(L, file));
}


static int
dir(struct plist *p, char *line, struct file_attr *a)
{
	char path[MAXPATHLEN+1];
	char *cp;
	struct stat st;
	int ret = EPKG_OK;

	cp = line + strlen(line) -1;
	while (cp > line && isspace(*cp)) {
		*cp = 0;
		cp--;
	}

	if (line[0] == '/')
		snprintf(path, sizeof(path), "%s/", line);
	else
		snprintf(path, sizeof(path), "%s%s%s/", p->prefix, p->slash,
		    line);

	if (fstatat(p->stagefd, RELATIVE_PATH(path), &st, AT_SYMLINK_NOFOLLOW)
	    == -1) {
		pkg_errno("Unable to access file %s%s",
		    p->stage ? p->stage: "", path);
		if (p->stage != NULL)
			ret = EPKG_FATAL;
		if (ctx.developer_mode) {
			pkg_emit_developer_mode("Plist error: @dir %s", line);
			ret = EPKG_FATAL;
		}
	} else {
		mode_t m = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
		if (a != NULL)
			ret = pkg_adddir_attr(p->pkg, path,
			    a->owner ? a->owner : p->uname,
			    a->group ? a->group : p->gname,
			    a->mode ? a->mode : p->perm,
			    a->fflags, true);
		else {
			ret = pkg_adddir_attr(p->pkg, path, p->uname, p->gname,
			    p->perm == 0 ? m : p->perm, 0, true);
		}
	}

	return (ret);
}

static int
meta_file(struct plist *p, char *line, struct file_attr *a, bool is_config)
{
	size_t len;
	char path[MAXPATHLEN];
	ssize_t linklen = 0;
	char symlink_target[MAXPATHLEN];
	struct stat st;
	char *buf = NULL;
	bool regular = false;
	int ret = EPKG_OK;

	len = strlen(line);

	while (isspace(line[len - 1]))
		line[--len] = '\0';

	if (line[0] == '/')
		snprintf(path, sizeof(path), "%s", line);
	else
		snprintf(path, sizeof(path), "%s%s%s", p->prefix,
		    p->slash, line);

	if (fstatat(p->stagefd, RELATIVE_PATH(path), &st, AT_SYMLINK_NOFOLLOW)
	    == -1) {
		pkg_errno("Unable to access file %s%s",
		    p->stage ? p->stage : "", path);
		if (p->stage != NULL)
			ret = EPKG_FATAL;
		if (ctx.developer_mode) {
			pkg_emit_developer_mode("Plist error, missing file: %s",
			    line);
			ret = EPKG_FATAL;
		}
		return (ret);
	}
	buf = NULL;
	regular = false;

	if (S_ISREG(st.st_mode)) {
		if (st.st_nlink > 1)
			regular = !check_for_hardlink(&p->hardlinks, &st);
		else
			regular = true;
	} else if (S_ISLNK(st.st_mode)) {
		regular = false;
		linklen = readlinkat(p->stagefd, RELATIVE_PATH(path),
				     symlink_target, sizeof(symlink_target) - 1);
		if (linklen == -1) {
			pkg_emit_errno("meta_file", "readlink failed");
			return (EPKG_FATAL);
		}
		symlink_target[linklen] = '\0';
	}

	buf = pkg_checksum_generate_fileat(p->stagefd, RELATIVE_PATH(path),
	    PKG_HASH_TYPE_SHA256_HEX);
	if (buf == NULL) {
		return (EPKG_FATAL);
	}

	if (regular) {
		p->flatsize += st.st_size;
		if (is_config) {
			off_t sz;
			char *content;
			file_to_bufferat(p->stagefd, RELATIVE_PATH(path),
			    &content, &sz);
			ret = pkg_addconfig_file(p->pkg, path, content);
			if (ret != EPKG_OK) {
				return (ret);
			}
			free(content);
		}
	}

	if (S_ISDIR(st.st_mode)) {
		pkg_emit_error("Plist error, directory listed as a file: %s",
		    line);
		free(buf);
		return (EPKG_FATAL);
	}

	if (a != NULL) {
		ret = pkg_addfile_attr(p->pkg, path, buf,
				       a->owner ? a->owner : p->uname,
				       a->group ? a->group : p->gname,
				       a->mode ? a->mode : p->perm,
				       a->fflags, st.st_mtim.tv_sec,
				       linklen > 0 ? symlink_target : NULL,
				       true);
	} else {
		ret = pkg_addfile_attr(p->pkg, path, buf, p->uname,
				       p->gname, p->perm, 0, st.st_mtim.tv_sec,
				       linklen > 0 ? symlink_target : NULL,
				       true);
	}

	free(buf);

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
setmod(struct plist *p, char *line, struct file_attr *a __unused)
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
	return (EPKG_OK);
}

static int
setowner(struct plist *p, char *line, struct file_attr *a __unused)
{
	free(p->uname);
	if (line[0] == '\0')
		p->uname = xstrdup("root");
	else
		p->uname = xstrdup(line);
	return (EPKG_OK);
}

static int
setgroup(struct plist *p, char *line, struct file_attr *a __unused)
{
	free(p->gname);
	if (line[0] == '\0')
		p->gname = xstrdup("wheel");
	else
		p->gname = xstrdup(line);
	return (EPKG_OK);
}

static int
comment_key(struct plist *p __unused, char *line __unused , struct file_attr *a __unused)
{
	/* ignore md5 will be recomputed anyway */
	return (EPKG_OK);
}

static struct keyact {
	const char *key;
	int (*action)(struct plist *, char *, struct file_attr *);
} keyacts[] = {
	{ "cwd", setprefix },
	{ "comment", comment_key },
	{ "config", config },
	{ "dir", dir },
	{ "include", include_plist },
	{ "mode", setmod },
	{ "owner", setowner },
	{ "group", setgroup },
	{ "override_prefix", override_prefix },
	{ "var", add_variable },
	/* old pkg compat */
	{ "name", name_key },
	{ NULL, NULL },
};

static struct lua_map {
	const char *key;
	pkg_lua_script type;
} lua_mapping[] = {
	{ "pre-install-lua", PKG_LUA_PRE_INSTALL },
	{ "post-install-lua", PKG_LUA_POST_INSTALL },
	{ "pre-deinstall-lua", PKG_LUA_PRE_DEINSTALL },
	{ "post-deinstall-lua", PKG_LUA_POST_DEINSTALL },
};

static struct script_map {
	const char *key;
	pkg_script type;
} script_mapping[] = {
	{ "pre-install", PKG_SCRIPT_PRE_INSTALL },
	{ "post-install", PKG_SCRIPT_POST_INSTALL },
	{ "pre-deinstall", PKG_SCRIPT_PRE_DEINSTALL },
	{ "post-deinstall", PKG_SCRIPT_POST_DEINSTALL },
};

static void
populate_keywords(struct plist *p)
{
	struct keyword *k;
	struct action *a;
	int i;

	for (i = 0; keyacts[i].key != NULL; i++) {
		k = xcalloc(1, sizeof(struct keyword));
		a = xmalloc(sizeof(struct action));
		k->keyword = xstrdup(keyacts[i].key);
		a->perform = keyacts[i].action;
		vec_push(&k->actions, a);
		pkghash_safe_add(p->keywords, k->keyword, k, NULL);
	}
}

static void
keyword_free(struct keyword *k)
{
	free(k->keyword);
	vec_free_and_free(&k->actions, free);
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
	int r, rc = EPKG_OK;

	while ((cur = ucl_iterate_object(o, &it, true))) {
		actname = ucl_object_tostring(cur);
		for (i = 0; list_actions[i].name != NULL; i++) {
			if (!strncasecmp(actname, list_actions[i].name,
			    list_actions[i].namelen) &&
			    (actname[list_actions[i].namelen ] == '\0' ||
			     actname[list_actions[i].namelen ] == '(' )) {
				actname += list_actions[i].namelen;
				if (*actname == '(') {
					if (strspn(actname + 1, "1234567890")
					    != strlen(actname + 1) - 1) {
						pkg_emit_error(
						    "Invalid argument: "
						    "expecting a number "
						    "got %s", actname);
						return (EPKG_FATAL);
					}
					j = strtol(actname+1, NULL, 10);
					if (j > argc) {
						pkg_emit_error(
						    "Invalid argument requested %d"
						    " available: %d", j, argc);
						return (EPKG_FATAL);
					}
				}
				r = list_actions[i].perform(p, j > 0 ? argv[j - 1] : line, a);
				if (r != EPKG_OK && rc == EPKG_OK)
					rc = r;
				break;
			}
		}
	}

	return (rc);
}

static void
parse_attributes(const ucl_object_t *o, struct file_attr **a)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *key;

	if (*a == NULL)
		*a = xcalloc(1, sizeof(struct file_attr));

	while ((cur = ucl_iterate_object(o, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;
		if (STRIEQ(key, "owner") && cur->type == UCL_STRING) {
			free((*a)->owner);
			(*a)->owner = xstrdup(ucl_object_tostring(cur));
			continue;
		}
		if (STRIEQ(key, "group") && cur->type == UCL_STRING) {
			free((*a)->group);
			(*a)->group = xstrdup(ucl_object_tostring(cur));
			continue;
		}
		if (STRIEQ(key, "mode")) {
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

static void
append_script(struct plist *p, pkg_script t, const char *cmd)
{
	switch (t) {
	case PKG_SCRIPT_PRE_INSTALL:
		fprintf(p->pre_install_buf->fp, "%s\n", cmd);
		break;
	case PKG_SCRIPT_POST_INSTALL:
		fprintf(p->post_install_buf->fp, "%s\n", cmd);
		break;
	case PKG_SCRIPT_PRE_DEINSTALL:
		fprintf(p->pre_deinstall_buf->fp, "%s\n", cmd);
		break;
	case PKG_SCRIPT_POST_DEINSTALL:
		fprintf(p->post_deinstall_buf->fp, "%s\n", cmd);
		break;
	}
}

static int
apply_keyword_file(ucl_object_t *obj, struct plist *p, char *line, struct file_attr *attr)
{
	const ucl_object_t *o, *cur, *elt;
	ucl_object_iter_t it = NULL;
	struct pkg_message *msg;
	char *cmd;
	const char *l = line;
	char *formated_line = NULL;
	char **args = NULL;
	char *buf, *tofree = NULL;
	struct file_attr *freeattr = NULL;
	int spaces, argc = 0;
	int ret = EPKG_FATAL;

	if ((o = ucl_object_find_key(obj,  "arguments")) && ucl_object_toboolean(o)) {
		spaces = pkg_utils_count_spaces(line);
		args = xmalloc((spaces + 1)* sizeof(char *));
		tofree = buf = xstrdup(line);
		while (buf != NULL) {
			args[argc++] = pkg_utils_tokenize(&buf);
		}
	}

	if ((o = ucl_object_find_key(obj,  "attributes")))
		parse_attributes(o, attr != NULL ? &attr : &freeattr);

	if ((o = ucl_object_find_key(obj,  "preformat_arguments")) &&
	    ucl_object_toboolean(o)) {
		format_exec_cmd(&formated_line, line, p->prefix, p->last_file, NULL, 0,
				NULL, false);
		l = formated_line;
	}
	/* add all shell scripts */
	for (int i = 0; i < NELEM(script_mapping); i++) {
		if ((o = ucl_object_find_key(obj, script_mapping[i].key))) {
			if (format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix,
			    p->last_file, l, argc, args, false) != EPKG_OK)
				goto keywords_cleanup;
			append_script(p, script_mapping[i].type, cmd);
			free(cmd);
		}
	}

	/* add all lua scripts */
	for (int i = 0; i < NELEM(lua_mapping); i++) {
		if ((o = ucl_object_find_key(obj, lua_mapping[i].key))) {
			if (format_exec_cmd(&cmd, ucl_object_tostring(o), p->prefix,
			    p->last_file, l, argc, args, true) != EPKG_OK)
				goto keywords_cleanup;
			pkg_add_lua_script(p->pkg, cmd, lua_mapping[i].type);
			free(cmd);
		}
	}
	free(formated_line);

	if ((o = ucl_object_find_key(obj, "messages"))) {
		while ((cur = ucl_iterate_object(o, &it, true))) {
			elt = ucl_object_find_key(cur, "message");
			msg = xcalloc(1, sizeof(*msg));
			msg->str = xstrdup(ucl_object_tostring(elt));
			msg->type = PKG_MESSAGE_ALWAYS;
			elt = ucl_object_find_key(cur, "type");
			if (elt != NULL) {
				if (STRIEQ(ucl_object_tostring(elt), "install"))
					msg->type = PKG_MESSAGE_INSTALL;
				else if (STRIEQ(ucl_object_tostring(elt), "remove"))
					msg->type = PKG_MESSAGE_REMOVE;
				else if (STRIEQ(ucl_object_tostring(elt), "upgrade"))
					msg->type = PKG_MESSAGE_UPGRADE;
			}
			vec_push(&p->pkg->message, msg);
		}
	}

	ret = EPKG_OK;
	if ((o = ucl_object_find_key(obj,  "actions")))
		ret = parse_actions(o, p, line, attr, argc, args);

	if (ret == EPKG_OK && (o = ucl_object_find_key(obj, "prepackaging"))) {
		lua_State *L = luaL_newstate();
		static const luaL_Reg plist_lib[] = {
			{ "config", lua_config },
			{ "dir", lua_dir },
			{ "file", lua_file },
			{ NULL, NULL },
		};
		luaL_openlibs(L);
		lua_pushlightuserdata(L, p);
		lua_setglobal(L, "plist");
		lua_pushlightuserdata(L, attr);
		lua_setglobal(L, "attrs");
		lua_pushstring(L, line);
		lua_setglobal(L, "line");
		lua_args_table(L, args, argc);
		luaL_newlib(L, plist_lib);
		lua_setglobal(L, "pkg");
		lua_override_ios(L, false);
		pkg_debug(3, "Scripts: executing lua\n--- BEGIN ---"
		    "\n%s\nScripts: --- END ---", ucl_object_tostring(o));
		if (luaL_dostring(L, ucl_object_tostring(o))) {
			pkg_emit_error("Failed to execute lua script: "
			    "%s", lua_tostring(L, -1));
			ret = EPKG_FATAL;
		}
		if (lua_tonumber(L, -1) != 0) {
			ret = EPKG_FATAL;
		}
		lua_close(L);
	}

keywords_cleanup:
	free(args);
	free(tofree);
	return (ret);
}

static int
external_keyword(struct plist *plist, char *keyword, char *line, struct file_attr *attr)
{
	struct ucl_parser *parser;
	const char *keyword_dir = NULL;
	char keyfile_path[MAXPATHLEN];
	int ret = EPKG_UNKNOWN, fd;
	ucl_object_t *o, *schema;
	const ucl_object_t *obj;
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

	fd = open(keyfile_path, O_RDONLY);
	if (fd == -1) {
		pkg_emit_error("cannot load keyword from %s: %s",
				keyfile_path, strerror(errno));
		return (EPKG_UNKNOWN);
	}

	parser = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_fd(parser, fd)) {
		pkg_emit_error("cannot parse keyword: %s",
				ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		close(fd);
		return (EPKG_UNKNOWN);
	}

	close(fd);
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

	if ((obj = ucl_object_find_key(o, "deprecated")) &&
	    ucl_object_toboolean(obj)) {
		obj = ucl_object_find_key(o, "deprecation_message");
		pkg_emit_error("Use of '@%s' is deprecated%s%s", keyword,
		   obj != NULL ? ": " : "",
		   obj != NULL ? ucl_object_tostring(obj) : "");
		if (ctx.developer_mode) {
			ucl_object_unref(o);
			return (EPKG_FATAL);
		}
	}
	ret = apply_keyword_file(o, plist, line, attr);
	if (ret != EPKG_OK) {
		pkg_emit_error("Fail to apply keyword '%s'", keyword);
	}

	return (ret);
}

 struct file_attr *
parse_keyword_args(char *args, char *keyword)
{
	struct file_attr *attr;
	char *owner, *group, *permstr, *fflags;
	void *set = NULL;
	u_long fset = 0;

	owner = group = permstr = fflags = NULL;

	/* remove last ')' */
	args[strlen(args) -1] = '\0';

	do {
		args[0] = '\0';
		args++;
		while (isspace(*args))
			args++;
		if (*args == '\0')
			break;
		if (owner == NULL) {
			owner = args;
		} else if (group == NULL) {
			group = args;
		} else if (permstr == NULL) {
			permstr = args;
		} else if (fflags == NULL) {
			fflags = args;
			break;
		} else {
			return (NULL);
		}
	} while ((args = strchr(args, ',')) != NULL);

	if (fflags != NULL && *fflags != '\0') {
#ifdef HAVE_STRTOFFLAGS
		if (strtofflags(&fflags, &fset, NULL) != 0) {
			pkg_emit_error("Malformed keyword '%s', wrong fflags",
			    keyword);
			return (NULL);
		}
#else
		pkg_emit_error("Malformed keyword '%s', maximum 3 arguments "
		    "are accepted", keyword);
#endif
	}

	if (permstr != NULL && *permstr != '\0') {
		if ((set = parse_mode(permstr)) == NULL) {
			pkg_emit_error("Malformed keyword '%s', wrong mode "
			    "section", keyword);
			return (NULL);
		}
	}
	if (owner == NULL && group == NULL && set == NULL)
		return (NULL);

	attr = xcalloc(1, sizeof(struct file_attr));
	if (owner != NULL && *owner != '\0')
		attr->owner = xstrdup(rtrimspace(owner));
	if (group != NULL && *group != '\0')
		attr->group = xstrdup(rtrimspace(group));
	if (set != NULL) {
		attr->mode = getmode(set, 0);
		free(set);
	}
	attr->fflags = fset;

	return (attr);
}

static int
parse_keywords(struct plist *plist, char *keyword,
    char *line, struct file_attr *attr)
{
	struct keyword *k = NULL;
	int ret = EPKG_FATAL;

	/* if keyword is empty consider it as a file */
	if (*keyword == '\0')
		return (file(plist, line, attr));

	k = pkghash_get_value(plist->keywords, keyword);
	if (k != NULL) {
		vec_foreach(k->actions, i) {
			ret = k->actions.d[i]->perform(plist, line, attr);
			if (ret != EPKG_OK)
				break;
		}
		return (ret);
	}

	/*
	 * if we are here it means the keyword has not been found
	 * maybe it is defined externally
	 * let's try to find it
	 */
	return (external_keyword(plist, keyword, line, attr));
}

char *
extract_keywords(char *line, char **keyword, struct file_attr **attr)
{
	char *k, *buf, *tmp;
	struct file_attr *a = NULL;

	buf = k = line;
	while (!(isspace(buf[0]) || buf[0] == '\0')) {
		if (buf[0] == '(' && (buf = strchr(buf, ')')) == NULL)
			return (NULL);
		buf++;
	}
	if (buf[0] != '\0') {
		buf[0] = '\0';
		buf++;
	}

	/* trim spaces after the keyword */
	while (isspace(buf[0]))
		buf++;

	pkg_debug(1, "Parsing plist, found keyword: '%s", k);

	if ((tmp = strchr(k, '(')) != NULL && k[strlen(k) -1] != ')')
		return (NULL);

	if (tmp != NULL) {
		a = parse_keyword_args(tmp, k);
		if (a == NULL)
			return (NULL);
	}

	*attr = a;
	*keyword = k;

	return (buf);
}

static void
flush_script_buffer(xstring *buf, struct pkg *p, int type)
{
	fflush(buf->fp);
	if (buf->buf[0] != '\0') {
		pkg_appendscript(p, buf->buf, type);
	}
}

int
plist_parse_line(struct plist *plist, char *line)
{
	char *buf, *bkpline;

	if (line[0] == '\0')
		return (EPKG_OK);

	pkg_debug(1, "Parsing plist line: '%s'", line);
	bkpline = xstrdup(line);

	if (line[0] == '@') {
		char *keyword = 0;
		struct file_attr *a = 0;
		buf = extract_keywords(line + 1, &keyword, &a);
		if (buf == NULL) {
			pkg_emit_error("Malformed keyword %s, expecting @keyword "
			    "or @keyword(owner,group,mode)", bkpline);
			free_file_attr(a);
			free(bkpline);
			return (EPKG_FATAL);
		}

		switch (parse_keywords(plist, keyword, buf, a)) {
		case EPKG_UNKNOWN:
			pkg_emit_error("unknown keyword %s: %s",
			    keyword, line);
			/* FALLTHRU */
		case EPKG_FATAL:
			free_file_attr(a);
			free(bkpline);
			return (EPKG_FATAL);
		}
		free_file_attr(a);
	} else {
		buf = line;
		strlcpy(plist->last_file, buf, sizeof(plist->last_file));

		/* remove spaces at the begining and at the end */
		while (isspace(buf[0]))
			buf++;

		if (file(plist, buf, NULL) != EPKG_OK) {
			free(bkpline);
			return (EPKG_FATAL);
		}
	}

	free(bkpline);
	return (EPKG_OK);
}

struct plist *
plist_new(struct pkg *pkg, const char *stage)
{
	struct plist *p;

	p = xcalloc(1, sizeof(struct plist));
	p->plistdirfd = -1;
	p->stagefd = open(stage ? stage : "/", O_DIRECTORY | O_CLOEXEC);
	if (p->stagefd == -1) {
		free(p);
		return (NULL);
	}

	p->pkg = pkg;
	if (pkg->prefix != NULL)
		strlcpy(p->prefix, pkg->prefix, sizeof(p->prefix));
	p->slash = *p->prefix != '\0' && p->prefix[strlen(p->prefix) - 1] == '/' ? "" : "/";
	p->stage = stage;

	p->uname = xstrdup("root");
	p->gname = xstrdup("wheel");

	p->pre_install_buf = xstring_new();
	p->post_install_buf = xstring_new();
	p->pre_deinstall_buf = xstring_new();
	p->post_deinstall_buf = xstring_new();

	populate_keywords(p);

	return (p);
}

void
plist_free(struct plist *p)
{
	if (p == NULL)
		return;

	if (p->stagefd != -1)
		close(p->stagefd);
	if (p->plistdirfd != -1)
		close(p->plistdirfd);

	pkghash_it it = pkghash_iterator(p->keywords);
	while (pkghash_next(&it))
		keyword_free((struct keyword *)it.value);
	pkghash_destroy(p->keywords);
	p->keywords = NULL;

	free(p->uname);
	free(p->gname);
	vec_free_and_free(&p->hardlinks, free);

	xstring_free(p->post_deinstall_buf);
	xstring_free(p->post_install_buf);
	xstring_free(p->pre_deinstall_buf);
	xstring_free(p->pre_install_buf);

	free(p);
}

char *
expand_plist_variables(const char *in, kvlist_t *vars)
{
	xstring *buf;
	const char *cp;
	size_t len;

	if (vec_len(vars) == 0)
		return (xstrdup(in));

	buf = xstring_new();
	cp = NULL;
	while (in[0] != '\0') {
		if (in[0] != '%') {
			fputc(in[0], buf->fp);
			in++;
			continue;
		}
		in++;
		if (in[0] == '\0') {
			fputc('%', buf->fp);
			break;
		}
		if (in[0] != '%') {
			fputc('%', buf->fp);
			fputc(in[0], buf->fp);
			in++;
			continue;
		}
		in++;
		cp = in;
		while (in[0] != '\0' && !isspace(in[0])) {
			if (in[0] == '%' && in[1] == '%') {
				in++;
				break;
			}
			in++;
		}
		if (in[0] != '%') {
			fprintf(buf->fp, "%%%%%.*s", (int)(in - cp), cp);
			continue;
		}
		len = in - cp -1;
		/* we have a variable */
		bool found = false;
		vec_foreach(*vars, i) {
			if (strncmp(cp, vars->d[i]->key, len) != 0)
				continue;
			fputs(vars->d[i]->value, buf->fp);
			found = true;
			in++;
			break;
		}
		if (found)
			continue;
		fprintf(buf->fp, "%%%%%.*s%%", (int)(in - cp), cp);
		in++;
	}
	return (xstring_get(buf));
}

static int
plist_parse(struct plist *pplist, FILE *f)
{
	int ret, rc = EPKG_OK;
	size_t linecap = 0;
	ssize_t linelen;
	char *line = NULL;
	char *l;

	while ((linelen = getline(&line, &linecap, f)) > 0) {
		if (line[linelen - 1] == '\n')
			line[linelen - 1] = '\0';
		l = expand_plist_variables(line, &pplist->variables);
		ret = plist_parse_line(pplist, l);
		free(l);
		if (ret != EPKG_OK && rc == EPKG_OK)
			rc = ret;
	}
	free(line);

	return (rc);
}

static int
open_directory_of(const char *file)
{
	char path[MAXPATHLEN];
	char *walk;

	if (strchr(file, '/') == NULL) {
		if (getcwd(path, MAXPATHLEN) == NULL) {
			pkg_emit_error("Unable to determine current location");
			return (-1);
		}
		return (open(path, O_DIRECTORY));
	}
	strlcpy(path, file, sizeof(path));
	walk = strrchr(path, '/');
	*walk = '\0';
	return (open(path, O_DIRECTORY));
}

int
add_variable(struct plist *p, char *line, struct file_attr *a __unused)
{
	const char *key;
	char *val;

	key = val = line;
	while (*val != '\0' && !isspace(*val))
		val++;
	if (*val != '\0') {
		*val = '\0';
		val++;
	}

	if (*key == '\0') {
		pkg_emit_error("Inside in @include it is not allowed to reuse @include");
		return (EPKG_FATAL);
	}

	while (*val != '\0' && isspace(*val))
		val++;

	vec_foreach(p->variables, i) {
		if (STREQ(p->variables.d[i]->key, key)) {
			free(p->variables.d[i]->value);
			p->variables.d[i]->value = xstrdup(val);
			return (EPKG_OK);
		}
	}
	struct pkg_kv *kv = pkg_kv_new(key, val);
	vec_push(&p->variables, kv);
	return (EPKG_OK);
}

int
include_plist(struct plist *p, char *name, struct file_attr *a __unused)
{
	FILE *f;
	int fd;
	int rc;

	if (p->in_include) {
		pkg_emit_error("Inside in @include it is not allowed to reuse @include");
		return (EPKG_FATAL);
	}
	p->in_include = true;

	fd = openat(p->plistdirfd, name, O_RDONLY);
	if (fd == -1) {
		pkg_emit_errno("Inpossible to include", name);
		return (EPKG_FATAL);
	}
	f = fdopen(fd, "r");
	if (f == NULL) {
		pkg_emit_errno("Inpossible to include", name);
		close(fd);
		return (EPKG_FATAL);
	}

	rc = plist_parse(p, f);

	fclose(f);
	return (rc);
}

int
ports_parse_plist(struct pkg *pkg, const char *plist, const char *stage)
{
	int rc = EPKG_OK;
	struct plist *pplist;
	FILE *plist_f;

	assert(pkg != NULL);
	assert(plist != NULL);

	if ((pplist = plist_new(pkg, stage)) == NULL)
		return (EPKG_FATAL);

	pplist->plistdirfd = open_directory_of(plist);
	if (pplist->plistdirfd == -1) {
		pkg_emit_error("impossible to open the directory where the plist is: %s", plist);
		plist_free(pplist);
		return (EPKG_FATAL);
	}
	if ((plist_f = fopen(plist, "re")) == NULL) {
		pkg_emit_error("Unable to open plist file: %s", plist);
		plist_free(pplist);
		return (EPKG_FATAL);
	}

	rc = plist_parse(pplist, plist_f);

	pkg->flatsize = pplist->flatsize;

	flush_script_buffer(pplist->pre_install_buf, pkg,
	    PKG_SCRIPT_PRE_INSTALL);
	flush_script_buffer(pplist->post_install_buf, pkg,
	    PKG_SCRIPT_POST_INSTALL);
	flush_script_buffer(pplist->pre_deinstall_buf, pkg,
	    PKG_SCRIPT_PRE_DEINSTALL);
	flush_script_buffer(pplist->post_deinstall_buf, pkg,
	    PKG_SCRIPT_POST_DEINSTALL);

	fclose(plist_f);

	plist_free(pplist);

	return (rc);
}

/*
 * if the provided database is NULL then we don't want to register the package
 * in the database aka NO_PKG_REGISTER
 */
int
pkg_add_port(struct pkgdb *db, struct pkg *pkg, const char *input_path,
    const char *reloc, bool testing)
{
	const char *location;
	int rc = EPKG_OK;
	xstring *message;

	if (db != NULL && pkg_is_installed(db, pkg->name) != EPKG_END) {
		return(EPKG_INSTALLED);
	}

	location = reloc;
	if (ctx.pkg_rootdir != NULL)
		location = ctx.pkg_rootdir;

	if (ctx.pkg_rootdir == NULL && location != NULL)
		pkg_kv_add(&pkg->annotations, "relocated", location, "annotation");

	pkg_emit_install_begin(pkg);

	if (db != NULL) {
		rc = pkgdb_register_pkg(db, pkg, 0, NULL);
		if (rc != EPKG_OK) {
			db = NULL;
			goto cleanup;
		}
	}

	if (!testing) {
		/* Execute pre-install scripts */
		pkg_lua_script_run(pkg, PKG_LUA_PRE_INSTALL, false);
		pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL, false, false);

		if (input_path != NULL) {
			pkg_register_cleanup_callback(pkg_rollback_cb, pkg);
			rc = pkg_add_fromdir(pkg, input_path, db);
			pkg_unregister_cleanup_callback(pkg_rollback_cb, pkg);
			if (rc != EPKG_OK) {
				pkg_rollback_pkg(pkg);
				if (db != NULL)
					pkg_delete_dirs(db, pkg, NULL);
			}
		}

		/* Execute post-install scripts */
		pkg_lua_script_run(pkg, PKG_LUA_POST_INSTALL, false);
		pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL, false, false);
	}

	if (rc == EPKG_OK) {
		pkg_emit_install_finished(pkg, NULL);
		if (pkg_has_message(pkg))
			message = xstring_new();
		vec_foreach(pkg->message, i) {
			if (pkg->message.d[i]->type == PKG_MESSAGE_ALWAYS ||
			    pkg->message.d[i]->type == PKG_MESSAGE_INSTALL) {
				fprintf(message->fp, "%s\n", pkg->message.d[i]->str);
			}
		}
		if (pkg_has_message(pkg)) {
			fflush(message->fp);
			if (message->buf[0] != '\0') {
				pkg_emit_message(message->buf);
			}
			xstring_free(message);
		}
	}
	/* it is impossible at this point to get any cleanup triggers to run */
	triggers_execute(NULL);

cleanup:
	if (db != NULL)
		pkgdb_register_finale(db, rc, NULL);

	return (rc);
}
