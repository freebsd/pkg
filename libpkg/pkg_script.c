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
