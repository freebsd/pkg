#include <stdlib.h>

#include "pkg.h"
#include "pkg_private.h"

const char *
pkg_file_path(struct pkg_file *file)
{
	return (file->path);
}

const char *
pkg_file_sha256(struct pkg_file *file)
{
	return (file->sha256);
}

int64_t
pkg_file_size(struct pkg_file *file)
{
	return (file->size);
}

int
pkg_file_new(struct pkg_file **file)
{
	if ((*file = calloc(1, sizeof(struct pkg_file))))
		return (-1);

	(*file)->size = -1;
	return (0);
}

void
pkg_file_reset(struct pkg_file *file)
{
	file->path[0] = '\0';
	file->sha256[0] = '\0';
	file->size = -1;
}

void
pkg_file_free(struct pkg_file *file)
{
	free(file);
}
