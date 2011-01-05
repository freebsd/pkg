#include <stdlib.h>

#include "pkg.h"
#include "pkg_private.h"

const char *
pkg_conflict_origin(struct pkg_conflict *c)
{
	return (c->origin);
}

const char *
pkg_conflict_version(struct pkg_conflict *c)
{
	return (c->version);
}

const char *
pkg_conflict_name(struct pkg_conflict *c)
{
	return (c->name);
}

int
pkg_conflict_new(struct pkg_conflict **c)
{
	if ((*c = calloc(1, sizeof(struct pkg_conflict))))
		return (-1);
	return (0);
}

void
pkg_conflict_reset(struct pkg_conflict *c)
{
	c->origin[0] = '\0';
	c->version[0] = '\0';
	c->name[0] = '\0';
}

void
pkg_conflict_free(struct pkg_conflict *c)
{
	free(c);
}
