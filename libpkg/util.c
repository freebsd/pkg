#include <sys/stat.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "util.h"

void
array_init(struct array *a, size_t c)
{
	assert(c > 0);

	/* if the array is already initialized, do nothing */
	if (a->cap > 0 && a->data != NULL)
		return;

	a->cap = c;
	a->len = 0;
	a->data = malloc(sizeof(void*) * a->cap);
}

void
array_append(struct array *a, void *d)
{
	assert(a->cap > 0);
	assert(a->data != NULL);

	if (a->cap <= a->len) {
		a->cap *= 2;
		a->data = realloc(a->data, sizeof(void*) * a->cap);
	}
	a->data[a->len++] = d;
}

void
array_reset(struct array *a, void (*free_elm)(void*))
{
	if (a->data == NULL)
		return;

	if (free_elm != NULL)
		for (size_t i = 0; i < a->len; i++)
			free_elm(a->data[i]);
	a->len = 0;
	a->data[0] = NULL;
}

void
array_free(struct array *a, void (*free_elm)(void*))
{
	if (a->data == NULL)
		return;

	if (free_elm != NULL)
		for (size_t i = 0; i < a->len; i++)
			free_elm(a->data[i]);
	free(a->data);
	a->data = NULL;
	a->len = 0;
	a->cap = 0;
}

off_t
file_to_buffer(const char *path, char **buffer)
{
	int fd;
	struct stat st;

	assert(path != NULL);
	assert(buffer != NULL);

	if ((fd = open(path, O_RDONLY)) == -1) {
		warn("open(%s)", path);
		return (-1);
	}

	if (fstat(fd, &st) == -1) {
		warn("fstat(%d)", fd);
		close(fd);
		return (-1);
	}

	if ((*buffer = malloc(st.st_size + 1)) == NULL) {
		warn("malloc(%llu)", (unsigned long long)st.st_size + 1);
		close(fd);
		return (-1);
	}

	if (read(fd, *buffer, st.st_size) == -1) {
		warn("read()");
		close(fd);
		return (-1);
	}

	close(fd);

	/* NULL terminate the buffer so it can be used by stdio.h functions */
	(*buffer)[st.st_size] = '\0';

	return (st.st_size);
}

char *
str_replace(char *string, const char *find, char *replace)
{
	char *str, *end, *begin;
	size_t offset, replace_len, find_len;

	replace_len = strlen(replace);
	find_len = strlen(find);
	begin = string;
	str = NULL;
	offset = 0;

	while ((end = strstr(begin, find)) != NULL) {
		str = realloc(str, offset + replace_len + (end - begin));
		memcpy(str + offset, begin, end - begin);
		memcpy(str + offset + (end - begin), replace, replace_len);
		offset += (end - begin + replace_len);
		begin = end + find_len;
	}
	str = realloc(str, offset + strlen(begin) +1);
	memcpy(str+offset, begin, strlen(begin)+1);

	return (str);
}

int
select_dir(const struct dirent *dirent)
{
	if (dirent->d_type == DT_DIR && strcmp(dirent->d_name, ".") != 0
		&& strcmp(dirent->d_name, "..") != 0)
		return (1);

	return (0);
}
