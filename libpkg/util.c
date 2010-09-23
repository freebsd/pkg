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
