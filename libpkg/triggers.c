/*-
 * Copyright (c) 2020-2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "pkg_config.h"

#if __has_include(<sys/capsicum.h>)
#include <sys/capsicum.h>
#define HAVE_CAPSICUM 1
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

#include <pkg.h>
#include <private/pkg.h>
#include <private/event.h>
#include <private/lua.h>

extern char **environ;

static const unsigned char litchar[] =
"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static script_type_t
get_script_type(const char *str)
{
	if (STRIEQ(str, "lua"))
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
		"      anyOf = [{"
		"        type = array; "
		"        item = { type = string };"
		"      }, {"
		"        type = string;"
		"      }]"
		"    };"
		"    path_glob: { "
		"      anyOf = [{"
		"        type = array; "
		"        item = { type = string };"
		"      }, {"
		"        type = string;"
		"      }]"
		"    };"
		"    path_regexp: { "
		"      anyOf = [{"
		"        type = array; "
		"        item = { type = string };"
		"      }, {"
		"        type = string;"
		"      }]"
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
		"  required = [ trigger ];"
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

static bool
parse_trigger_script_block(const ucl_object_t *block, const char *block_name,
    const char *trigger_name, char **out_script, int *out_type, bool *out_sandbox)
{
	const ucl_object_t *o;

	o = ucl_object_find_key(block, "type");
	if (o == NULL) {
		pkg_emit_error("%s %s doesn't have a script type", block_name, trigger_name);
		return (false);
	}
	*out_type = get_script_type(ucl_object_tostring(o));
	if (*out_type == SCRIPT_UNKNOWN) {
		pkg_emit_error("Unknown script type for %s in %s", block_name, trigger_name);
		return (false);
	}
	o = ucl_object_find_key(block, "script");
	if (o == NULL) {
		pkg_emit_error("No script in %s %s", block_name, trigger_name);
		return (false);
	}
	*out_script = xstrdup(ucl_object_tostring(o));
	o = ucl_object_find_key(block, "sandbox");
	*out_sandbox = (o == NULL) ? true : ucl_object_toboolean(o);
	return (true);
}

static struct trigger *
trigger_load(int dfd, const char *name, bool cleanup_only, ucl_object_t *schema)
{
	struct ucl_parser *p;
	ucl_object_t *obj = NULL;
	const ucl_object_t *block = NULL;
	int fd;
	struct ucl_schema_error err;
	struct trigger *t;

	fd = openat(dfd, name, O_RDONLY);
	if (fd == -1) {
		pkg_emit_error("Unable to open the trigger: %s", name);
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
		pkg_emit_error("trigger definition %s cannot be validated: %s", name, err.msg);
		ucl_object_unref(obj);
		return (NULL);
	}

	t = xcalloc(1, sizeof(*t));
	t->name = xstrdup(name);

	if (cleanup_only) {
		block = ucl_object_find_key(obj, "cleanup");
		if (block == NULL)
			goto err;
		if (!parse_trigger_script_block(block, "cleanup", name,
		    &t->cleanup.script, &t->cleanup.type, &t->cleanup.sandbox))
			goto err;
		ucl_object_unref(obj);
		return (t);
	}

	block = ucl_object_find_key(obj, "trigger");
	if (!parse_trigger_script_block(block, "trigger", name,
	    &t->script.script, &t->script.type, &t->script.sandbox))
		goto err;

	/* Load path patterns (required for any non-cleanup block) */
	block = ucl_object_find_key(obj, "path");
	if (block != NULL)
		t->path = ucl_object_ref(block);
	block = ucl_object_find_key(obj, "path_glob");
	if (block != NULL)
		t->path_glob = ucl_object_ref(block);
	block = ucl_object_find_key(obj, "path_regexp");
	if (block != NULL)
		t->path_regexp = ucl_object_ref(block);
	if (t->path == NULL &&
	    t->path_glob == NULL &&
	    t->path_regexp == NULL) {
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
		if (t->path_regexp != NULL)
			ucl_object_unref(t->path_regexp);
		free(t->script.script);
		free(t->cleanup.script);
		free(t);
	}
	ucl_object_unref(obj);
	return (NULL);
}

void
trigger_is_it_a_cleanup(struct triggers *t, const char *path)
{
	const char *trigger_name, *dir;
	const pkg_object *dirs, *cur;
	struct trigger *trig;
	pkg_iter it;

	if (t->schema == NULL)
		t->schema = trigger_open_schema();

	/* Check if the file was installed in a trigger directory. */
	it = NULL;
	trigger_name = NULL;
	dirs = pkg_config_get("PKG_TRIGGERS_DIR");
	while ((cur = pkg_object_iterate(dirs, &it))) {
		size_t len;

		dir = pkg_object_string(cur);
		len = strlen(dir);

		if (strncmp(path, dir, len) == 0) {
			trigger_name = path + strlen(dir);
			break;
		}
	}

	if (trigger_name == NULL)
		return;

	if (t->dfd == -1)
		t->dfd = openat(ctx.rootfd, RELATIVE_PATH(dir), O_DIRECTORY);

	trig = trigger_load(t->dfd, RELATIVE_PATH(trigger_name), true, t->schema);
	if (trig != NULL) {
		if (t->cleanup == NULL)
			t->cleanup = xcalloc(1, sizeof(*t->cleanup));

		vec_push(t->cleanup, trig);
	}
}

/*
 * Load triggers from a specific directory and add them to a vec.
 */
static void
triggers_load_from(trigger_t *triggers, bool cleanup_only, const char *dir)
{
	int dfd;
	DIR *d;
	struct dirent *e;
	struct trigger *t;
	ucl_object_t *schema;
	struct stat st;

	dfd = openat(ctx.rootfd, dir, O_DIRECTORY);
	if (dfd == -1) {
		if (errno != ENOENT)
			pkg_emit_error("Unable to open the trigger directory %s: %s",
			    dir, strerror(errno));
		return;
	}

	d = fdopendir(dfd);
	if (d == NULL) {
		pkg_emit_error("Unable to open the trigger directory %s: %s",
		    dir, strerror(errno));
		close(dfd);
		return;
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
		if (!STREQ(ext, ".ucl"))
			continue;
		/* only regular files are considered */
		if (fstatat(dfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			pkg_emit_errno("fstatat", e->d_name);
			continue;
		}
		if (!S_ISREG(st.st_mode))
			continue;
		t = trigger_load(dfd, e->d_name, cleanup_only, schema);
		if (t != NULL)
			vec_push(triggers, t);
	}

	closedir(d);
	ucl_object_unref(schema);
}

/*
 * Load triggers from PKG_TRIGGERS_DIR.
 */
trigger_t *
triggers_load(bool cleanup_only)
{
	trigger_t *ret;
	const pkg_object *dirs, *cur;
	pkg_iter it = NULL;

	ret = xcalloc(1, sizeof(*ret));

	dirs = pkg_config_get("PKG_TRIGGERS_DIR");
	while ((cur = pkg_object_iterate(dirs, &it))) {
		const char *dir;

		dir = RELATIVE_PATH(pkg_object_string(cur));
		triggers_load_from(ret, cleanup_only, dir);
	}

	return (ret);
}

void
trigger_free(struct trigger *t)
{
	if (!t)
		return;
	free(t->name);
	if (t->path)
		ucl_object_unref(t->path);
	if (t->path_glob)
		ucl_object_unref(t->path_glob);
	if (t->path_regexp)
		ucl_object_unref(t->path_regexp);
	free(t->cleanup.script);
	free(t->script.script);
	free(t);
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
trigger_execute_lua_common(const char *script, bool sandbox, pkghash *args,
    bool defer, const char *pkgname, const char *pkgversion, bool upgrade)
{
	lua_State *L;
	int pstat;
	pkghash_it it;

	if (defer && !sandbox && ctx.defer_triggers) {
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
		if (pkgname != NULL) {
			lua_pushstring(L, pkgname);
			lua_setglobal(L, "pkg_name");
		}
		if (pkgversion != NULL) {
			lua_pushstring(L, pkgversion);
			lua_setglobal(L, "pkg_version");
		}
		lua_pushboolean(L, upgrade);
		lua_setglobal(L, "pkg_upgrade");
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
#ifndef COVERAGE
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

static int
trigger_execute_lua(const char *script, bool sandbox, pkghash *args)
{
	return (trigger_execute_lua_common(script, sandbox, args,
	    true, NULL, NULL, false));
}

static void
trigger_check_match(struct trigger *t, char *dir)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it;

	if (t->path != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(t->path, &it, true))) {
			if (STREQ(dir, ucl_object_tostring(cur))) {
				pkghash_safe_add(t->matched, dir, dir, NULL);
				return;
			}
		}
	}

	if (match_ucl_lists(dir, t->path_glob, t->path_regexp)) {
		pkghash_safe_add(t->matched, dir, dir, NULL);
	}
}

/*
 * first execute cleanup scripts from the triggers that are not there anymore,
 * then execute all per-transaction triggers that matched touched directories.
 * Always reload triggers from disk: new trigger files may have been installed
 * during the transaction.
 */
int
triggers_execute(struct triggers *t)
{
	trigger_t *triggers;
	int ret = EPKG_OK;

	triggers = triggers_load(false);

	pkg_emit_triggers_begin();
	if (t != NULL && t->cleanup != NULL) {
		vec_foreach(*t->cleanup, i) {
			pkg_emit_trigger(t->cleanup->d[i]->name, true);
			if (t->cleanup->d[i]->cleanup.type == SCRIPT_LUA) {
				ret = trigger_execute_lua(t->cleanup->d[i]->cleanup.script,
				    t->cleanup->d[i]->cleanup.sandbox, NULL);
			}
			if (ret != EPKG_OK)
				goto cleanup;
		}
	}

	if (ctx.touched_dir_hash) {
		pkghash_it it = pkghash_iterator(ctx.touched_dir_hash);
		while (pkghash_next(&it)) {
			vec_foreach(*triggers, i)
				trigger_check_match(triggers->d[i], it.key);
		}
	}

	vec_foreach(*triggers, i) {
		if (triggers->d[i]->matched == NULL)
			continue;
		pkg_emit_trigger(triggers->d[i]->name, false);
		if (triggers->d[i]->script.type == SCRIPT_LUA) {
			ret = trigger_execute_lua(triggers->d[i]->script.script,
			    triggers->d[i]->script.sandbox, triggers->d[i]->matched);
		}
		if (ret != EPKG_OK)
			goto cleanup;
	}
	pkg_emit_triggers_finished();

cleanup:
	vec_free_and_free(triggers, trigger_free);
	free(triggers);

	return (EPKG_OK);
}

/*
 * Match a dir against a trigger's path patterns, adding to local_matched.
 */
static bool
trigger_check_match_local(struct trigger *t, const char *dir, pkghash **matched)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it;

	if (t->path != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(t->path, &it, true))) {
			if (STREQ(dir, ucl_object_tostring(cur))) {
				pkghash_safe_add(*matched, dir, NULL, NULL);
				return (true);
			}
		}
	}

	if (match_ucl_lists(dir, t->path_glob, t->path_regexp)) {
		pkghash_safe_add(*matched, dir, NULL, NULL);
		return (true);
	}
	return (false);
}

/*
 * Per-package trigger subdirectory names, indexed by trigger_phase_t.
 */
static const char *trigger_phase_dirs[] = {
	[TRIGGER_PHASE_PRE_INSTALL] = "pre_install",
	[TRIGGER_PHASE_POST_INSTALL] = "post_install",
	[TRIGGER_PHASE_PRE_DEINSTALL] = "pre_deinstall",
	[TRIGGER_PHASE_POST_DEINSTALL] = "post_deinstall",
};

/*
 * Load per-package triggers from the phase-specific subdirectory
 * under each configured PKG_TRIGGERS_DIR.
 */
static trigger_t *
triggers_load_perpackage(trigger_phase_t phase)
{
	trigger_t *ret;
	const pkg_object *dirs, *cur;
	pkg_iter it = NULL;
	char path[MAXPATHLEN];

	ret = xcalloc(1, sizeof(*ret));

	dirs = pkg_config_get("PKG_TRIGGERS_DIR");
	while ((cur = pkg_object_iterate(dirs, &it))) {
		const char *dir;

		dir = RELATIVE_PATH(pkg_object_string(cur));
		snprintf(path, sizeof(path), "%s/%s",
		    dir, trigger_phase_dirs[phase]);
		triggers_load_from(ret, false, path);
	}

	return (ret);
}

/*
 * Execute per-package triggers for a given phase on a specific package.
 * Triggers are loaded from the phase-specific subdirectory (e.g.
 * post_install/) so only relevant triggers are parsed.  They are
 * reloaded from disk each time so that triggers installed or removed
 * by earlier packages in the same transaction are picked up.
 */
int
triggers_execute_perpackage(struct triggers *t, struct pkg *pkg,
    trigger_phase_t phase, bool upgrade)
{
	struct pkg_file *f = NULL;
	struct pkg_dir *d = NULL;
	pkghash *pkg_dirs_hash = NULL;
	int ret = EPKG_OK;
	trigger_t *triggers;

	if (t == NULL)
		return (EPKG_OK);

	triggers = triggers_load_perpackage(phase);

	if (triggers->len == 0) {
		vec_free_and_free(triggers, trigger_free);
		free(triggers);
		return (EPKG_OK);
	}

	/* Build set of parent directories from the package's files */
	while (pkg_files(pkg, &f) == EPKG_OK) {
		char *dir, *slash;
		dir = xstrdup(f->path);
		slash = strrchr(dir, '/');
		if (slash != NULL) {
			*slash = '\0';
			pkghash_safe_add(pkg_dirs_hash, dir, NULL, NULL);
		}
		free(dir);
	}
	while (pkg_dirs(pkg, &d) == EPKG_OK) {
		pkghash_safe_add(pkg_dirs_hash, d->path, NULL, NULL);
	}

	if (pkg_dirs_hash == NULL) {
		vec_free_and_free(triggers, trigger_free);
		free(triggers);
		return (EPKG_OK);
	}

	/* Match and execute */
	vec_foreach(*triggers, i) {
		struct trigger *trig = triggers->d[i];
		pkghash *local_matched = NULL;
		pkghash_it it = pkghash_iterator(pkg_dirs_hash);

		while (pkghash_next(&it))
			trigger_check_match_local(trig, it.key, &local_matched);

		if (local_matched == NULL)
			continue;

		pkg_emit_trigger(trig->name, false);
		if (trig->script.type == SCRIPT_LUA) {
			ret = trigger_execute_lua_common(trig->script.script,
			    trig->script.sandbox, local_matched, false,
			    pkg->name, pkg->version, upgrade);
		}
		pkghash_destroy(local_matched);
		if (ret != EPKG_OK) {
			pkg_emit_error("per-package trigger %s failed "
			    "for %s, continuing", trig->name, pkg->name);
			ret = EPKG_OK;
		}
	}

	pkghash_destroy(pkg_dirs_hash);
	vec_free_and_free(triggers, trigger_free);
	free(triggers);
	return (ret);
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

static void
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
	closedir(d);
	return (EPKG_OK);
}
