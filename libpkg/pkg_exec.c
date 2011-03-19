#include <stdlib.h>
#include <err.h>

#include "pkg.h"
#include "pkg_private.h"

const char *
pkg_script_data(struct pkg_script *s)
{
	return (sbuf_get(s->data));
}

pkg_script_t
pkg_script_type(struct pkg_script *s)
{
	return (s->type);
}

int
pkg_script_new(struct pkg_script **script)
{
	if ((*script = calloc(1, sizeof(struct pkg_script))) == NULL)
		err(EXIT_FAILURE, "calloc()");

	return (0);
}

void
pkg_script_reset(struct pkg_script *script)
{
	sbuf_reset(script->data);
}

void
pkg_script_free(struct pkg_script *script)
{
	if (script == NULL)
		return;

	sbuf_free(script->data);
	free(script);
}

void
pkg_script_free_void(void *s)
{
	if (s != NULL)
		pkg_script_free((struct pkg_script *)s);
}

const char *
pkg_exec_cmd(struct pkg_exec *exec)
{
	return (sbuf_get(exec->cmd));
}

pkg_exec_t
pkg_exec_type(struct pkg_exec *exec)
{
	return (exec->type);
}

int
pkg_exec_new(struct pkg_exec **exec)
{
	if ((*exec = calloc(1, sizeof(struct pkg_exec))) == NULL)
		err(EXIT_FAILURE, "calloc()");

	return (0);
}

void
pkg_exec_reset(struct pkg_exec *exec)
{
	sbuf_reset(exec->cmd);
}

void
pkg_exec_free(struct pkg_exec *exec)
{
	if (exec == NULL)
		return;

	sbuf_free(exec->cmd);
	free(exec);
}

void
pkg_exec_free_void(void *e)
{
	if (e != NULL)
		pkg_exec_free((struct pkg_exec *)e);
}
