#ifndef _PKG_UTIL_H
#define _PKG_UTIL_H

#include <sys/types.h>
#include <sys/sbuf.h>
#include <sys/param.h>

#define ARRAY_INIT {0, 0, NULL}

struct array {
	size_t cap;
	size_t len;
	void **data;
};

#define STARTS_WITH(string, needle) (strncasecmp(string, needle, strlen(needle)) == 0)

int sbuf_set(struct sbuf **, const char *);
const char * sbuf_get(struct sbuf *);
void sbuf_reset(struct sbuf *);
void sbuf_free(struct sbuf *);

int file_to_buffer(const char *, char **, off_t *);
int format_exec_cmd(char **, const char *, const char *, const char *);
int split_chr(char *, char);
int file_fetch(const char *, const char *);
int is_dir(const char *);
int is_conf_file(const char *path, char newpath[MAXPATHLEN]);

int sha256_file(const char *, char[65]);
void sha256_str(const char *, char[65]);
#endif
