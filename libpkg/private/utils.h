/*-
 * Copyright (c) 2011-2022 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _PKG_UTIL_H
#define _PKG_UTIL_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <ucl.h>
#include <tllist.h>
#include <pkg.h>
#include <xstring.h>

#define STARTS_WITH(string, needle) (strncasecmp(string, needle, strlen(needle)) == 0)
#define RELATIVE_PATH(p) (p + (*p == '/' ? 1 : 0))

#define ERROR_SQLITE(db, query) do { \
	pkg_emit_error("sqlite error while executing %s in file %s:%d: %s", query, \
	__FILE__, __LINE__, sqlite3_errmsg(db)); \
} while(0)

#define ERROR_STMT_SQLITE(db, statement) do { \
	pkg_emit_error("sqlite error while executing %s in file %s:%d: %s", sqlite3_expanded_sql(statement), \
	__FILE__, __LINE__, sqlite3_errmsg(db)); \
} while (0)

typedef tll(struct hardlink *) hardlinks_t;
struct hardlink {
	ino_t ino;
	dev_t dev;
	const char *path;
};

struct tempdir {
	char name[PATH_MAX];
	char temp[PATH_MAX];
	size_t len;
	int fd;
};
typedef tll(struct tempdir *) tempdirs_t;

struct dns_srvinfo {
	unsigned int type;
	unsigned int class;
	unsigned int ttl;
	unsigned int priority;
	unsigned int weight;
	unsigned int port;
	unsigned int finalweight;
	char host[MAXHOSTNAMELEN];
	struct dns_srvinfo *next;
};

struct pkg_key;

int32_t string_hash_func(const char *);

int mkdirs(const char *path);
int file_to_buffer(const char *, char **, off_t *);
int file_to_bufferat(int, const char *, char **, off_t *);
int format_exec_cmd(char **, const char *, const char *, const char *, const char *,
    int argc, char **argv, bool lua);
int is_dir(const char *);
int is_link(const char *);

int rsa_new(struct pkg_key **, pkg_password_cb *, char *path);
void rsa_free(struct pkg_key *);
int rsa_sign(char *path, struct pkg_key *keyinfo, unsigned char **sigret,
    unsigned int *siglen);
int rsa_verify(const char *key, unsigned char *sig, unsigned int sig_len, int fd);
int rsa_verify_cert(unsigned char *cert,
    int certlen, unsigned char *sig, int sig_len, int fd);

bool check_for_hardlink(hardlinks_t *hl, struct stat *st);
bool is_valid_abi(const char *arch, bool emit_error);
bool is_valid_os_version(struct pkg *pkg);

struct dns_srvinfo *
	dns_getsrvinfo(const char *zone);

int set_nameserver(const char *nsname);
void set_blocking(int fd);
void set_nonblocking(int fd);

int pkg_symlink_cksum(const char *path, const char *root, char *cksum);
int pkg_symlink_cksumat(int fd, const char *path, const char *root,
    char *cksum);

pid_t process_spawn_pipe(FILE *inout[2], const char *command);

void *parse_mode(const char *str);
int *text_diff(char *a, char *b);
int merge_3way(char *pivot, char *v1, char *v2, xstring *out);
bool mkdirat_p(int fd, const char *path);
int get_socketpair(int *);
int checkflags(const char *mode, int *optr);
bool match_ucl_lists(const char *buffer, const ucl_object_t *globs, const ucl_object_t *regexes);
char *get_dirname(char *dir);
char *rtrimspace(char *buf);
bool copy_file(int from, int to);
void hidden_tempfile(char *buf, int buflen, const char *path);
void append_random_suffix(char *buf, int buflen, int suffixlen);
char *json_escape(const char *str);
struct tempdir *open_tempdir(int rootfd, const char *path);

#endif
