#include <stdlib.h>

#include "pkg.h"
#include "pkg_private.h"

const char *
pkg_conflict_origin(struct pkg_conflict *c)
{
	return (sbuf_data(c->origin));
}

const char *
pkg_conflict_name(struct pkg_conflict *c)
{
	return (sbuf_data(c->name));
}

const char *
pkg_conflict_version(struct pkg_conflict *c)
{
	return (sbuf_data(c->version));
}

int
pkg_conflict_new(struct pkg_conflict **c)
{
	if ((*c = calloc(1, sizeof(struct pkg_conflict))))
		return (-1);

	(*c)->origin = sbuf_new_auto();
	(*c)->name = sbuf_new_auto();
	(*c)->version = sbuf_new_auto();

	return (0);
}

void
pkg_conflict_reset(struct pkg_conflict *c)
{
	sbuf_clear(c->origin);
	sbuf_clear(c->name);
	sbuf_clear(c->version);
}

void
pkg_conflict_free(struct pkg_conflict *c)
{
	sbuf_delete(c->origin);
	sbuf_delete(c->name);
	sbuf_delete(c->version);

	free(c);
}
