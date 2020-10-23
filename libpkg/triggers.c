/*-
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

#include "pkg_config.h"

#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
#endif

#include <sys/stat.h>
#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <spawn.h>

#include <private/pkg.h>
#include <private/event.h>
#include <private/lua.h>

extern char **environ;

static script_type_t
get_script_type(const char *str)
{
	if (strcasecmp(str, "lua") == 0)
		return (SCRIPT_LUA);
	if (strcasecmp(str, "shell") == 0)
		return (SCRIPT_SHELL);
	return (SCRIPT_UNKNOWN);
}

static ucl_object_t *
trigger_open_schema(void)
{
	struct ucl_parser *parser;
	ucl_object_t *trigger_schema;
	static const char trigger_schema_str[] = ""
		"{"
		"  type = object;"
		"  properties {"
		"    description: { type = string };"
		"    path: { "
		"      type = array; "
		"         item = { type = string };"
		"    };"
		"    path_glob: ( "
		"      type = array; "
		"      item = { type = string };"
		"    };"
		"    path_regexp: ( "
		"      type = array; "
		"      item = { type = string };"
		"    };"
		"  };"
		"  cleanup = { "
		"     type = object; "
		"     properties = {"
		"       type = { "
		"         type = string,"
		"         enum: [lua, shell];"
		"        };"
		"       script = { type = string };"
		"     }; "
		"     required = [ type, script ];"
		"  };"
		"  postexec = { "
		"     type = object; "
		"     properties = {"
		"       type = { "
		"         type = string,"
		"         enum: [lua, shell];"
		"        };"
		"       script = { type = string };"
		"     }; "
		"     required = [ type, script ];"
		"  };"
		"  required [ description ];"
		"}";

	parser = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_chunk(parser, trigger_schema_str,
	    sizeof(trigger_schema_str) -1)) {
		pkg_emit_error("Cannot parse schema for trigger: %s",
		    ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return (NULL);
	}

	trigger_schema = ucl_parser_get_object(parser);
	ucl_parser_free(parser);
	return (trigger_schema);
}

static struct trigger *
trigger_load(int dfd, const char *name, bool cleanup_only, ucl_object_t *schema)
{
	struct ucl_parser *p;
	ucl_object_t *obj = NULL;
	const ucl_object_t *o = NULL;
	int fd;
	struct ucl_schema_error err;
	struct trigger *t;

	fd = openat(dfd, name, O_RDONLY);
	if (fd == -1) {
		pkg_emit_error("Unable to open the tigger: %s", name);
		return (NULL);
	}

	p = ucl_parser_new(0);
	if (!ucl_parser_add_fd(p, fd)) {
		pkg_emit_error("Error parsing trigger '%s': %s", name,
		    ucl_parser_get_error(p));
		ucl_parser_free(p);
		close(fd);
		return (NULL);
	}
	close(fd);

	obj = ucl_parser_get_object(p);
	ucl_parser_free(p);
	if (obj == NULL)
		return (NULL);

	if (!ucl_object_validate(schema, obj, &err)) {
		pkg_emit_error("Keyword definition %s cannot be validated: %s", name, err.msg);
		ucl_object_unref(obj);
		return (NULL);
	}

	if (cleanup_only && ((o = ucl_object_find_key(obj, "cleanup")) == NULL)) {
		ucl_object_unref(obj);
		return (NULL);
	}

	t = xcalloc(1, sizeof(*t));
	t->desc = xstrdup(ucl_object_tostring(ucl_object_find_key(obj, "description")));
	t->name = xstrdup(name);
	if (o != NULL) {
		t->cleanup.type = get_script_type(ucl_object_tostring(ucl_object_find_key(o, "type")));
		t->cleanup.script = xstrdup(ucl_object_tostring(ucl_object_find_key(o, "type")));
	}
	if (cleanup_only) {
		ucl_object_unref(obj);
		return (t);
	}
	o = ucl_object_find_key(obj, "script");
	if (o != NULL) {
		t->script.type = get_script_type(ucl_object_tostring(ucl_object_find_key(o, "type")));
		t->script.script = xstrdup(ucl_object_tostring(ucl_object_find_key(o, "type")));
	}

	ucl_object_unref(obj);
	return (t);
}

struct trigger *
triggers_load(bool cleanup_only)
{
	int dfd;
	DIR *d;
	struct dirent *e;
	struct trigger *triggers, *t;
	ucl_object_t *schema;
	struct stat st;

	triggers = NULL;

	dfd = openat(ctx.rootfd, RELATIVE_PATH(ctx.triggers_path), O_DIRECTORY);
	if (dfd == -1) {
		pkg_emit_error("Unable to open the trigger directory");
		return (NULL);
	}
	d = fdopendir(dfd);
	if (d == NULL) {
		pkg_emit_error("Unable to open the trigger directory");
		close(dfd);
		return (NULL);
	}

	schema = trigger_open_schema();

	while ((e = readdir(d)) != NULL) {
		/* ignore all hidden files */
		if (e->d_name[0] ==  '.')
			continue;
		/* only regular files are considered */
		if (fstatat(dfd, e->d_name, &st, AT_SYMLINK_FOLLOW) != 0) {
			pkg_emit_errno("fstatat", e->d_name);
		}
		t = trigger_load(dfd, e->d_name, cleanup_only, schema);
		if (t != NULL)
			DL_APPEND(triggers, t);
	}

	closedir(d);
	ucl_object_unref(schema);
	return (triggers);
}

void
trigger_free(struct trigger *t)
{
	if (t == NULL)
		return;
	free(t->name);
	free(t->desc);
	for (int i = 0; t->path != NULL && t->path[i]; i++) {
		free(t->path[i]);
	}
	for (int i = 0; t->path_glob != NULL && t->path_glob[i]; i++) {
		free(t->path_glob[i]);
	}
	for (int i = 0; t->path_glob != NULL && t->path_regex[i]; i++) {
		free(t->path_regex[i]);
	}
	free(t->path);
	free(t->path_glob);
	free(t->path_regex);
	free(t->cleanup.script);
	free(t->script.script);
}

static int
trigger_execute_shell(const char *script)
{
	posix_spawn_file_actions_t action;
	int stdin_pipe[2] = {-1, -1};
	const char *argv[3];
	const char *script_p;
	size_t len;
	ssize_t bw;
	int error, pstat;
	int ret = EPKG_OK;
	pid_t pid;

	if (pipe(stdin_pipe) < 0)
		return (EPKG_FATAL);

	posix_spawn_file_actions_init(&action);
	posix_spawn_file_actions_adddup2(&action, stdin_pipe[0], STDIN_FILENO);
	posix_spawn_file_actions_addclose(&action, stdin_pipe[1]);

	argv[0] = _PATH_BSHELL;
	argv[1] = "-s";
	argv[2] = NULL;

	if ((error = posix_spawn(&pid, _PATH_BSHELL, &action, NULL,
	    __DECONST(char **, argv), environ)) != 0) {
		errno = error;
		pkg_errno("Cannot run trigger script %s", script);
		posix_spawn_file_actions_destroy(&action);
		ret = EPKG_FATAL;
		goto cleanup;
	}
	posix_spawn_file_actions_destroy(&action);
	len = strlen(script);

	while (len > 0) {
		script_p = script;
		if ((bw = write(stdin_pipe[1], script_p, len)) == -1) {
			if (errno == EINTR)
				continue;
			ret = EPKG_FATAL;
			goto cleanup;
		}
		script_p += bw;
		len -= bw;
	}
	close(stdin_pipe[1]);

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR) {
			pkg_emit_error("waitpid() failed: %s", strerror(errno));
			ret = EPKG_FATAL;
			goto cleanup;
		}
	}

	if (WEXITSTATUS(pstat) != 0)
		ret = EPKG_FATAL;

cleanup:
	if (stdin_pipe[0] != -1)
		close(stdin_pipe[0]);
	if (stdin_pipe[1] != -1)
		close(stdin_pipe[1]);

	return (ret);
}

static int
trigger_execute_lua(const char *script)
{
	lua_State *L;
	int pstat;

	pid_t pid = fork();
	if (pid == 0) {
		L = luaL_newstate();
		luaL_openlibs(L);
		lua_override_ios(L);
#ifdef HAVE_CAPSICUM
		if (cap_enter() < 0 && errno != ENOSYS) {
			err(1, "cap_enter failed");
		}
#endif
		if (luaL_dostring(L, script)) {
			pkg_emit_error("Failed to execute lua trigger: "
					"%s", lua_tostring(L, -1));
			exit(1);
		}
		if (lua_tonumber(L, -1) != 0) {
			lua_close(L);
			exit(1);
		}
		lua_close(L);
		exit(0);
	} else if (pid < 0) {
		pkg_emit_errno("Cannot fork", "lua_script");
		return (EPKG_FATAL);
	}
	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR) {
			pkg_emit_error("waitpid() failed: %s", strerror(errno));
			return (EPKG_FATAL );
		}
	}
	if (WEXITSTATUS(pstat) != 0) {
		pkg_emit_error("lua script failed");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

/*
 * first execute all triggers then the cleanup scripts
 * from the triggers that are not there anymore
 * Then execute all triggers
 */
int
triggers_execute(struct trigger *cleanup_triggers)
{
	struct trigger *triggers, *t, *trigger;
	kh_strings_t *th = NULL;
	int ret = EPKG_OK;

	triggers = triggers_load(false);

	/*
	 * Generate a hash table to ease the lookup later
	 */
	if (cleanup_triggers != NULL) {
		LL_FOREACH(triggers, t) {
			kh_add(strings, th, t->name, t->name, free);
		}
	}

	/*
	 * only keep from the cleanup the one that are not anymore in triggers
	 */
	LL_FOREACH_SAFE(cleanup_triggers, trigger, t) {
		if (kh_contains(strings, th, trigger->name)) {
			DL_DELETE(cleanup_triggers, trigger);
			trigger_free(trigger);
		}
	}
	kh_free(strings, th, char, free);

	pkg_emit_trigger_begin();
	LL_FOREACH(cleanup_triggers, t) {
		pkg_emit_trigger(t->name, true);
		if (t->cleanup.type == SCRIPT_LUA) {
			ret = trigger_execute_lua(t->cleanup.script);
		} else if (t->cleanup.type == SCRIPT_SHELL) {
			ret = trigger_execute_shell(t->cleanup.script);
		}
		if (ret != EPKG_OK)
			goto cleanup;
	}

	LL_FOREACH(triggers, t) {
		pkg_emit_trigger(t->name, false);
		if (t->cleanup.type == SCRIPT_LUA) {
			ret = trigger_execute_lua(t->script.script);
		} else if (t->cleanup.type == SCRIPT_SHELL) {
			ret = trigger_execute_shell(t->script.script);
		}
		if (ret != EPKG_OK)
			goto cleanup;
	}
	pkg_emit_trigger_finished();

cleanup:
	DL_FOREACH_SAFE(cleanup_triggers, trigger, t) {
		DL_DELETE(cleanup_triggers, trigger);
		trigger_free(trigger);
	}

	DL_FOREACH_SAFE(triggers, trigger, t) {
		DL_DELETE(triggers, trigger);
		trigger_free(trigger);
	}
	return (EPKG_OK);
}
