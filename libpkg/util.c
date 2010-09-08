#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "util.h"

char *
file_to_buffer(const char *path)
{
	FILE *fs;
	struct stat st;
	char *buffer = NULL;

	if (stat(path, &st) == -1)
		return (NULL);

	if ((fs = fopen(path, "r")) == NULL)
		return (NULL);

	if ((buffer = malloc(st.st_size + 1)) == NULL)
		return (NULL);

	fread(buffer, st.st_size, 1, fs);
	fclose(fs);

	return buffer;
}
