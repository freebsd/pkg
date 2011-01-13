#include <stdlib.h>

#include "pkg.h"
#include "pkg_private.h"

const char *
pkg_conflict_glob(struct pkg_conflict *c)
{
	return (sbuf_data(c->glob));
}

int
pkg_conflict_new(struct pkg_conflict **c)
{
	if ((*c = calloc(1, sizeof(struct pkg_conflict))))
		return (-1);

	(*c)->glob = sbuf_new_auto();

	return (0);
}

void
pkg_conflict_reset(struct pkg_conflict *c)
{
	sbuf_clear(c->glob);
}

void
pkg_conflict_free(struct pkg_conflict *c)
{
	if (c == NULL)
		return;

	sbuf_delete(c->glob);
	free(c);
}

void
pkg_conflict_free_void(void *c)
{
	if (c != NULL)
		pkg_conflict_free((struct pkg_conflict *)c);
}
