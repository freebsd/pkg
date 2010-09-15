#ifndef _PKG_UTIL_H
#define _PKG_UTIL_H

off_t file_to_buffer(const char *path, char **buffer);
char *str_replace(char *string, const char *find, char *replace);
#endif
