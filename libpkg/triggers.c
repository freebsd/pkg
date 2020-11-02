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
#include <fnmatch.h>
#include <paths.h>
#include <regex.h>
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
		"      item = { type = string };"
		"    };"
		"    path_glob: { "
		"      type = array; "
		"      item = { type = string };"
		"    };"
		"    path_regexp: { "
		"      type = array; "
		"      item = { type = string };"
		"    };"
		"    cleanup = { "
		"      type = object; "
		"      properties = {"
		"        type = { "
		"          type = string,"
		"          enum: [lua, shell];"
		"        };"
		"        script = { type = string };"
		"      }; "
		"      required = [ type, script ];"
		"    };"
		"    trigger = { "
		"      type = object; "
		"      properties = {"
		"        type = { "
		"          type = string,"
		"          enum: [lua, shell];"
		"        };"
		"        script = { type = string };"
		"      }; "
		"      required = [ type, script ];"
		"    };"
		"  }\n"
		"  required = [ description, trigger ];"
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
	const ucl_object_t *o = NULL, *trigger = NULL, *cleanup = NULL;
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

	if (!ucl_object_validate(obj, schema, &err)) {
		pkg_emit_error("trigger definition %s cannot be validated: %s", name, err.msg);
		ucl_object_unref(obj);
		return (NULL);
	}

	t = xcalloc(1, sizeof(*t));

	if (cleanup_only) {
		cleanup = ucl_object_find_key(obj, "cleanup");
		if (cleanup == NULL)
			goto err;
		o = ucl_object_find_key(cleanup, "type");
		if (o == NULL) {
			pkg_emit_error("cleanup %s doesn't have a script type", name);
			goto err;
		}
		t->cleanup.type = get_script_type(ucl_object_tostring(o));
		if (t->cleanup.type == SCRIPT_UNKNOWN) {
			pkg_emit_error("Unknown script type for cleanup in %s", name);
			goto err;
		}
		o = ucl_object_find_key(cleanup, "script");
		if (o == NULL) {
			pkg_emit_error("No script in cleanup %s", name);
			goto err;
		}

		t->cleanup.script = xstrdup(ucl_object_tostring(o));
		ucl_object_unref(obj);
		return (t);
	}

	trigger = ucl_object_find_key(obj, "trigger");
	if (trigger == NULL) {
		pkg_emit_error("trigger %s doesn't have any trigger block, ignoring", name);
		goto err;
	}

	o = ucl_object_find_key(trigger, "type");
	if (o == NULL) {
		pkg_emit_error("trigger %s doesn't have a script type", name);
		goto err;
	}
	t->script.type = get_script_type(ucl_object_tostring(o));
	if (t->script.type == SCRIPT_UNKNOWN) {
		pkg_emit_error("Unknown script type for trigger in %s", name);
		goto err;
	}
	o = ucl_object_find_key(trigger, "script");
	if (o == NULL) {
		pkg_emit_error("No script in trigger %s", name);
		goto err;
	}

	t->script.script = xstrdup(ucl_object_tostring(o));

	o = ucl_object_find_key(obj, "path");
	if (o != NULL)
		t->path = ucl_object_ref(o);
	o = ucl_object_find_key(obj, "path_glob");
	if (o != NULL)
		t->path_glob = ucl_object_ref(o);
	o = ucl_object_find_key(obj, "path_regex");
	if (o != NULL)
		t->path_regex = ucl_object_ref(o);
	if (t->path == NULL &&
	    t->path_glob == NULL &&
	    t->path_regex == NULL) {
		pkg_emit_error("No path* in trigger %s, skipping", name);
		goto err;
	}

	ucl_object_unref(obj);
	return (t);

err:
	if (t) {
		if (t->path != NULL)
			ucl_object_unref(t->path);
		if (t->path_glob != NULL)
			ucl_object_unref(t->path_glob);
		if (t->path_regex != NULL)
			ucl_object_unref(t->path_regex);
		if (t->script.script != NULL)
			free(t->script.script);
		if (t->cleanup.script != NULL)
			free(t->cleanup.script);
		free(t);
	}
	ucl_object_unref(obj);
	return (NULL);
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
		if (errno != ENOENT)
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
		if (fstatat(dfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			pkg_emit_errno("fstatat", e->d_name);
			return (NULL);
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
	if (t->path != NULL)
		ucl_object_unref(t->path);
	if (t->path != NULL)
		ucl_object_unref(t->path_glob);
	if (t->path != NULL)
		ucl_object_unref(t->path_regex);
	free(t->path);
	free(t->path_glob);
	free(t->path_regex);
	free(t->cleanup.script);
	free(t->script.script);
}

static int
trigger_execute_shell(const char *script, kh_strings_t *args __unused)
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
trigger_execute_lua(const char *script, kh_strings_t *args)
{
	lua_State *L;
	int pstat;

	pid_t pid = fork();
	if (pid == 0) {
		L = luaL_newstate();
		luaL_openlibs(L);
		lua_override_ios(L);
		char *dir;
		char **arguments = NULL;
		int i = 0;
		if (args != NULL) {
			arguments = xcalloc(kh_count(args), sizeof(char*));
			kh_foreach_value(args, dir, {
				arguments[i++] = dir;
			});
		}
		lua_args_table(L, arguments, i);
#ifdef HAVE_CAPSICUM
		if (cap_enter() < 0 && errno != ENOSYS) {
			err(1, "cap_enter failed");
		}
#endif
		if (luaL_dostring(L, script)) {
			pkg_emit_error("Failed to execute lua trigger: "
					"%s", lua_tostring(L, -1));
			_exit(1);
		}
		if (lua_tonumber(L, -1) != 0) {
			lua_close(L);
			_exit(1);
		}
		lua_close(L);
		_exit(0);
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

static void
trigger_check_match(struct trigger *t, char *dir)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it;

	if (t->path != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(t->path, &it, true))) {
			if (strcmp(dir, ucl_object_tostring(cur)) == 0) {
				kh_safe_add(strings, t->matched, dir, dir);
				return;
			}
		}
	}

	if (t->path_glob != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(t->path_glob, &it, true))) {
			if (fnmatch(ucl_object_tostring(cur), dir, 0) == 0) {
				kh_safe_add(strings, t->matched, dir, dir);
				return;
			}
		}
	}

	if (t->path_regex != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(t->path_regex, &it, true))) {
			regex_t re;
			regcomp(&re, ucl_object_tostring(cur),
			   REG_EXTENDED|REG_NOSUB);
			if (regexec(&re, dir, 0, NULL, 0) == 0) {
				kh_safe_add(strings, t->matched, dir, dir);
				regfree(&re);
				return;
			}
			regfree(&re);
		}
	}
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
	char *dir;
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

	pkg_emit_triggers_begin();
	LL_FOREACH(cleanup_triggers, t) {
		pkg_emit_trigger(t->name, true);
		if (t->cleanup.type == SCRIPT_LUA) {
			ret = trigger_execute_lua(t->cleanup.script, NULL);
		} else if (t->cleanup.type == SCRIPT_SHELL) {
			ret = trigger_execute_shell(t->cleanup.script, NULL);
		}
		if (ret != EPKG_OK)
			goto cleanup;
	}

	if (ctx.touched_dir_hash) {
		kh_foreach_value(ctx.touched_dir_hash, dir, {
				LL_FOREACH(triggers, t) {
				trigger_check_match(t, dir);
				}
				/* We need to check if that matches a trigger */
				});
	}

	LL_FOREACH(triggers, t) {
		if (t->matched == NULL)
			continue;
		pkg_emit_trigger(t->name, false);
		if (t->script.type == SCRIPT_LUA) {
			ret = trigger_execute_lua(t->script.script, t->matched);
		} else if (t->script.type == SCRIPT_SHELL) {
			ret = trigger_execute_shell(t->script.script, t->matched);
		}
		if (ret != EPKG_OK)
			goto cleanup;
	}
	pkg_emit_triggers_finished();

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

void
append_touched_file(const char *path)
{
	char *newpath, *walk;

	newpath = xstrdup(path);
	walk = strrchr(newpath, '/');
	if (walk == NULL)
		return;
	*walk = '\0';

	kh_add(strings, ctx.touched_dir_hash, newpath, newpath, free);
}
