#ifndef _PKG_UTIL_H
#define _PKG_UTIL_H

#include <sys/types.h>
#include <sys/sbuf.h>

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

int sbuf_set(struct sbuf **, const char *);
const char * sbuf_get(struct sbuf *);
void sbuf_reset(struct sbuf *);
void sbuf_free(struct sbuf *);

int file_to_buffer(const char *, char **, off_t *);
int format_exec_cmd(char **, const char *, const char *, const char *);
int split_chr(char *, char);
int file_fetch(const char *, const char *);
int is_dir(const char *path);

#endif
