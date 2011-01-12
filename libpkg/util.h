#ifndef _PKG_UTIL_H
#define _PKG_UTIL_H

#include <sys/param.h>
#include <dirent.h>

struct array {
	size_t cap;
	size_t len;
	void **data;
};

#define STARTS_WITH(string, needle) (strncasecmp(string, needle, strlen(needle)) == 0)

void array_init(struct array *, size_t);
void array_append(struct array *, void *);
void array_reset(struct array *, void (*free_elm)(void*));
void array_free(struct array *, void (*free_elm)(void*));

off_t file_to_buffer(const char *path, char **buffer);
char *str_replace(char *string, const char *find, char *replace);
int select_dir(const struct dirent *);
int split_chr(char *, char);

#endif
