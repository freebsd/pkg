/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

struct hardlink {
	ino_t ino;
	dev_t dev;
	const char *path;
};
typedef vec_t(struct hardlink *) hardlinks_t;

struct tempdir {
	char name[PATH_MAX];
	char temp[PATH_MAX];
	size_t len;
	int fd;
};
typedef vec_t(struct tempdir *) tempdirs_t;

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

int file_to_buffer(const char *, char **, off_t *);
int file_to_bufferat(int, const char *, char **, off_t *);
int format_exec_cmd(char **, const char *, const char *, const char *, const char *,
    int argc, char **argv, bool lua);
int is_dir(const char *);
int is_link(const char *);

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
bool pkg_match_paths_list(const ucl_object_t *paths, const char *file);
char *get_dirname(char *dir);
char *rtrimspace(char *buf);
void hidden_tempfile(char *buf, int buflen, const char *path);
void append_random_suffix(char *buf, int buflen, int suffixlen);
char *json_escape(const char *str);
const char *get_http_auth(void);
bool c_charv_contains(c_charv_t *, const char *, bool);
bool charv_contains(charv_t *, const char *, bool);
bool str_ends_with(const char *str, const char *end);
int char_cmp(const void *a, const void *b);
const char *charv_search(charv_t *, const char *);

#endif
