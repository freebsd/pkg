#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "util.h"

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

/* TODO make a cleaner version */
char *
str_replace(char *string, const char *find, char *replace)
{
	char *prev, *tmp;
	char *new_string;
	size_t newlen;

	new_string = malloc(1);
	new_string[0] = '\0';
	prev = string;
	tmp = string;
	while ((tmp = strstr(tmp, find)) != NULL) {
		tmp[0] = '\0';
		tmp += strlen(find);
		newlen = strlen(new_string) + strlen(prev) + strlen(replace) + 1;
		new_string = realloc(new_string, newlen);
		strlcat(new_string, prev, newlen);
		strlcat(new_string, replace, newlen);
		prev = tmp;
	}
	newlen = strlen(new_string) + strlen(prev) + 1;
	strlcat(new_string, prev, newlen);

	return new_string;
}
