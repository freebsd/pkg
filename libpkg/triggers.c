/*-
 * Copyright (c) 2020-2022 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <xstring.h>

#include <private/pkg.h>
#include <private/event.h>
#include <private/lua.h>

extern char **environ;

static const unsigned char litchar[] =
"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static script_type_t
get_script_type(const char *str)
{
	if (strcasecmp(str, "lua") == 0)
		return (SCRIPT_LUA);
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
		"          sandbox = boolean, "
		"          enum: [lua];"
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
		"          sandbox = boolean, "
		"          enum: [lua];"
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
		pkg_emit_errno("plop", name);
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
	t->name = xstrdup(name);

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
		o = ucl_object_find_key(cleanup, "sandbox");
		if (o == NULL) {
			t->cleanup.sandbox = true;
		} else {
			t->cleanup.sandbox = ucl_object_toboolean(o);
		}
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
	o = ucl_object_find_key(trigger, "sandbox");
	if (o == NULL) {
		t->script.sandbox = true;
	} else {
		t->script.sandbox = ucl_object_toboolean(o);
	}

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

void
trigger_is_it_a_cleanup(struct triggers *t, const char *path)
{
	const char *trigger_name;
	struct trigger *trig;

	if (t->schema == NULL)
		t->schema = trigger_open_schema();
	if (strncmp(path, ctx.triggers_path, strlen(ctx.triggers_path)) != 0)
		return;

	trigger_name = path + strlen(ctx.triggers_path);

	if (t->dfd == -1)
		t->dfd = openat(ctx.rootfd, RELATIVE_PATH(ctx.triggers_path), O_DIRECTORY);
	trig = trigger_load(t->dfd, RELATIVE_PATH(trigger_name), true, t->schema);
	if (trig != NULL) {
		if (t->cleanup == NULL)
			t->cleanup = xcalloc(1, sizeof(*t->cleanup));

		tll_push_back(*t->cleanup, trig);
	}
}

trigger_t *
triggers_load(bool cleanup_only)
{
	int dfd;
	DIR *d;
	struct dirent *e;
	struct trigger *t;
	trigger_t *triggers = xcalloc(1, sizeof(*triggers));
	ucl_object_t *schema;
	struct stat st;

	dfd = openat(ctx.rootfd, RELATIVE_PATH(ctx.triggers_path), O_DIRECTORY);
	if (dfd == -1) {
		if (errno != ENOENT)
			pkg_emit_error("Unable to open the trigger directory");
		return (triggers);
	}
	d = fdopendir(dfd);
	if (d == NULL) {
		pkg_emit_error("Unable to open the trigger directory");
		close(dfd);
		return (triggers);
	}

	schema = trigger_open_schema();

	while ((e = readdir(d)) != NULL) {
		const char *ext;
		/* ignore all hidden files */
		if (e->d_name[0] ==  '.')
			continue;
		/* only consider files ending with .ucl */
		ext = strrchr(e->d_name, '.');
		if (ext == NULL)
			continue;
		if (strcmp(ext, ".ucl") != 0)
			continue;
		/* only regular files are considered */
		if (fstatat(dfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			pkg_emit_errno("fstatat", e->d_name);
			return (triggers);
		}
		if (!S_ISREG(st.st_mode))
			continue;
		t = trigger_load(dfd, e->d_name, cleanup_only, schema);
		if (t != NULL)
			tll_push_back(*triggers, t);
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
	free(t->cleanup.script);
	free(t->script.script);
}

static char *
get_random_name(char name[])
{
	char *pos;
	int r;

	pos = name;
	while (*pos == 'X') {
#ifndef HAVE_ARC4RANDOM
		r = rand() % (sizeof(litchar) -1);
#else
		r = arc4random_uniform(sizeof(litchar) -1);
#endif
		*pos++ = litchar[r];
	}

	return (name);
}

static void
save_trigger(const char *script, bool sandbox, pkghash *args)
{
	int db = ctx.pkg_dbdirfd;
	pkghash_it it;

	if (!mkdirat_p(db, "triggers"))
		return;

	int trigfd = openat(db, "triggers", O_DIRECTORY);
	close(db);
	if (trigfd == -1) {
		pkg_errno("Failed to open '%s' as a directory", "triggers");
		return;
	}

#ifndef HAVE_ARC4RANDOM
	srand(time(NULL));
#endif

	int fd;
	for (;;) {
		char name[] = "XXXXXXXXXX";
		fd = openat(trigfd, get_random_name(name),
		    O_CREAT|O_RDWR|O_EXCL, 0644);
		if (fd != -1)
			break;
		if (errno == EEXIST)
			continue;
		pkg_errno("Can't create deferred triggers %s", name);
		return;
	}
	close(trigfd);
	FILE *f = fdopen(fd, "w");
	if (sandbox)
		fputs("--sandbox\n", f);
	fputs("--begin args\n", f);
	it = pkghash_iterator(args);
	while (pkghash_next(&it)) {
		fprintf(f, "-- %s\n", (char *)it.value);
	}
	fputs("--end args\n--\n", f);
	fprintf(f, "%s\n", script);
	fclose(f);
}

static int
trigger_execute_lua(const char *script, bool sandbox, pkghash *args)
{
	lua_State *L;
	int pstat;
	pkghash_it it;

	if (ctx.defer_triggers) {
		save_trigger(script, sandbox, args);
		return (EPKG_OK);
	}

	pid_t pid = fork();
	if (pid == 0) {
		L = luaL_newstate();
		luaL_openlibs(L);
		lua_override_ios(L, sandbox);
		static const luaL_Reg pkg_lib[] = {
			{ "print_msg", lua_print_msg },
			{ "prefixed_path", lua_prefix_path },
			{ "filecmp", lua_pkg_filecmp },
			{ "copy", lua_pkg_copy },
			{ "stat", lua_stat },
			{ "readdir", lua_readdir },
			{ "exec", lua_exec },
			{ "symlink", lua_pkg_symlink },
			{ NULL, NULL },
		};
		luaL_newlib(L, pkg_lib);
		lua_setglobal(L, "pkg");
		lua_pushinteger(L, ctx.rootfd);
		lua_setglobal(L, "rootfd");
		char **arguments = NULL;
		int i = 0;
		if (args != NULL) {
			arguments = xcalloc(pkghash_count(args), sizeof(char*));
			it = pkghash_iterator(args);
			while (pkghash_next(&it)) {
				arguments[i++] = it.key;
			}
		}
		lua_args_table(L, arguments, i);
#ifdef HAVE_CAPSICUM
		if (sandbox) {
#ifndef PKG_COVERAGE
			if (cap_enter() < 0 && errno != ENOSYS) {
				err(1, "cap_enter failed");
			}
#endif
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
				pkghash_safe_add(t->matched, dir, dir, NULL);
				return;
			}
		}
	}

	if (match_ucl_lists(dir, t->path_glob, t->path_regex)) {
		pkghash_safe_add(t->matched, dir, dir, NULL);
	}
}

/*
 * first execute all triggers then the cleanup scripts
 * from the triggers that are not there anymore
 * Then execute all triggers
 */
int
triggers_execute(trigger_t *cleanup_triggers)
{
	trigger_t *triggers;
	int ret = EPKG_OK;

	triggers = triggers_load(false);
	pkg_emit_triggers_begin();
	if (cleanup_triggers != NULL) {
		tll_foreach(*cleanup_triggers, it) {
			pkg_emit_trigger(it->item->name, true);
			if (it->item->cleanup.type == SCRIPT_LUA) {
				ret = trigger_execute_lua(it->item->cleanup.script,
				    it->item->cleanup.sandbox, NULL);
			}
			if (ret != EPKG_OK)
				goto cleanup;
		}
	}

	if (ctx.touched_dir_hash) {
		pkghash_it it = pkghash_iterator(ctx.touched_dir_hash);
		while (pkghash_next(&it)) {
			tll_foreach(*triggers, t)
				trigger_check_match(t->item, it.key);
			/* We need to check if that matches a trigger */
		}
	}

	tll_foreach(*triggers, it) {
		if (it->item->matched == NULL)
			continue;
		pkg_emit_trigger(it->item->name, false);
		if (it->item->script.type == SCRIPT_LUA) {
			ret = trigger_execute_lua(it->item->script.script,
			    it->item->script.sandbox, it->item->matched);
		}
		if (ret != EPKG_OK)
			goto cleanup;
	}
	pkg_emit_triggers_finished();

cleanup:
	tll_free_and_free(*triggers, trigger_free);
	free(triggers);

	return (EPKG_OK);
}

void
append_touched_dir(const char *path)
{
	pkghash_safe_add(ctx.touched_dir_hash, path, NULL, NULL);
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

	pkghash_safe_add(ctx.touched_dir_hash, newpath, NULL, NULL );
	free(newpath);
}

void
exec_deferred(int dfd, const char *name)
{
	bool sandbox = false;
	pkghash *args = NULL;
	xstring *script = NULL;

	int fd = openat(dfd, name, O_RDONLY);
	if (fd == -1) {
		pkg_errno("Unable to open the trigger '%s'", name);
		return;
	}
	FILE *f = fdopen(fd, "r");
	if (f == NULL) {
		pkg_errno("Unable to open the trigger '%s'", name);
		return;
	}
	
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	char *walk;
	bool inargs = false;
	while ((linelen = getline(&line, &linecap, f)) > 0) {
		walk = line;
		walk += 2; /* '--' aka lua comments */
		if (strncmp(walk, "sandbox", 7) == 0) {
			sandbox = true;
			continue;
		}
		if (strncmp(walk, "begin args", 10) == 0) {
			inargs = true;
			continue;
		}
		if (strncmp(walk, "end args", 8) == 0) {
			inargs = false;
			script = xstring_new();
			continue;
		}
		if (inargs) {
			walk++; /* skip the space */
			if (line[linelen -1] == '\n')
				line[linelen -1] = '\0';
			pkghash_safe_add(args, walk, NULL, NULL);
		}
		if (script != NULL)
			fputs(line, script->fp);
	}
	free(line);
	fclose(f);
	if (script == NULL) {
		pkghash_destroy(args);
		return;
	}
	char *s = xstring_get(script);
	if (trigger_execute_lua(s, sandbox, args) == EPKG_OK) {
		unlinkat(dfd, name, 0);
	}
	free(s);
	pkghash_destroy(args);
}

int
pkg_execute_deferred_triggers(void)
{
	struct dirent *e;
	struct stat st;
	int dbdir = pkg_get_dbdirfd();

	int trigfd = openat(dbdir, "triggers", O_DIRECTORY);
	if (trigfd == -1)
		return (EPKG_OK);

	DIR *d = fdopendir(trigfd);
	if (d == NULL) {
		close(trigfd);
		pkg_emit_error("Unable to open the deferred trigger directory");
		return (EPKG_FATAL);
	}

	while ((e = readdir(d)) != NULL) {
		/* ignore all hiddn files */
		if (e->d_name[0] == '.')
			continue;
		/* only regular files are considered */
		if (fstatat(trigfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			pkg_emit_errno("fstatat", e->d_name);
			return (EPKG_FATAL);
		}
		exec_deferred(trigfd, e->d_name);
	}
	return (EPKG_OK);
}
