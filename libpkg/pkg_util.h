#ifndef _PKG_UTIL_H
#define _PKG_UTIL_H

#include <sys/types.h>
#include <sys/sbuf.h>
#include <sys/param.h>

#include <openssl/sha.h>

#define STARTS_WITH(string, needle) (strncasecmp(string, needle, strlen(needle)) == 0)

#define ERROR_SQLITE(db) \
	pkg_emit_error("sqlite: %s", sqlite3_errmsg(db))

int sbuf_set(struct sbuf **, const char *);
char * sbuf_get(struct sbuf *);
void sbuf_reset(struct sbuf *);
void sbuf_free(struct sbuf *);

int mkdirs(const char *path);
int file_to_buffer(const char *, char **, off_t *);
int format_exec_cmd(char **, const char *, const char *, const char *, char *);
int split_chr(char *, char);
int file_fetch(const char *, const char *);
int is_dir(const char *);
int is_conf_file(const char *path, char *newpath, size_t len);

int sha256_file(const char *, char[SHA256_DIGEST_LENGTH * 2 +1]);
void sha256_str(const char *, char[SHA256_DIGEST_LENGTH * 2 +1]);
#endif
