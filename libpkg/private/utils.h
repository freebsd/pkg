/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <sys/sbuf.h>
#include <sys/param.h>

#include <openssl/sha.h>

#define STARTS_WITH(string, needle) (strncasecmp(string, needle, strlen(needle)) == 0)

#define ERROR_SQLITE(db) \
	pkg_emit_error("sqlite: %s (%s:%d)", sqlite3_errmsg(db), __FILE__, __LINE__)

struct hardlinks {
	ino_t *inodes;
	size_t len;
	size_t cap;
};

struct dns_srvinfo {
	unsigned int type;
	unsigned int class;
	unsigned int ttl;
	unsigned int priority;
	unsigned int weight;
	unsigned int port;
	char host[MAXHOSTNAMELEN];
	struct dns_srvinfo *next;
};

void sbuf_init(struct sbuf **);
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

int rsa_sign(char *path, pem_password_cb *password_cb, char *rsa_key_path,
		 unsigned char **sigret, unsigned int *siglen);
int rsa_verify(const char *path, const char *key,
		unsigned char *sig, unsigned int sig_len);

bool is_hardlink(struct hardlinks *hl, struct stat *st);

struct dns_srvinfo *
	dns_getsrvinfo(const char *zone);

#endif
