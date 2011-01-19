#include <stdlib.h>

#include "pkg.h"
#include "pkg_private.h"

const char *
pkg_option_opt(struct pkg_option *option)
{
	return (sbuf_get(option->opt));
}

const char *
pkg_option_value(struct pkg_option *option)
{
	return (sbuf_get(option->value));
}

int
pkg_option_new(struct pkg_option **option)
{
	if ((*option = calloc(1, sizeof(struct pkg_option))))
		return (-1);
	return (0);
}

void
pkg_option_reset(struct pkg_option *option)
{
	sbuf_reset(option->opt);
	sbuf_reset(option->value);
}

void
pkg_option_free(struct pkg_option *option)
{
	sbuf_free(option->opt);
	sbuf_free(option->value);
}

void
pkg_option_free_void(void *o)
{
	if (o != NULL)
		pkg_option_free((struct pkg_option *)o);
}
