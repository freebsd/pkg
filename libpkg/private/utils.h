/*-
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
#include <uthash.h>

#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

#define STARTS_WITH(string, needle) (strncasecmp(string, needle, strlen(needle)) == 0)

#define ERROR_SQLITE(db) \
	pkg_emit_error("sqlite: %s", sqlite3_errmsg(db), __FILE__, __LINE__)

#define HASH_FIND_INO(head,ino,out)                                          \
	HASH_FIND(hh,head,ino,sizeof(ino_t),out)
#define HASH_ADD_INO(head,ino,add)                                          \
	HASH_ADD(hh,head,ino,sizeof(ino_t),add)

struct hardlinks {
	ino_t inode;
	UT_hash_handle hh;
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

struct rsa_key {
	pem_password_cb *pw_cb;
	char *path;
	RSA *key;
};


void sbuf_init(struct sbuf **);
int sbuf_set(struct sbuf **, const char *);
char * sbuf_get(struct sbuf *);
void sbuf_reset(struct sbuf *);
void sbuf_free(struct sbuf *);

int mkdirs(const char *path);
int file_to_buffer(const char *, char **, off_t *);
int format_exec_cmd(char **, const char *, const char *, const char *, char *);
int is_dir(const char *);
int is_conf_file(const char *path, char *newpath, size_t len);

int sha256_file(const char *, char[SHA256_DIGEST_LENGTH * 2 +1]);
int sha256_fd(int fd, char[SHA256_DIGEST_LENGTH * 2 +1]);
int md5_file(const char *, char[MD5_DIGEST_LENGTH * 2 +1]);

int rsa_new(struct rsa_key **, pem_password_cb *, char *path);
void rsa_free(struct rsa_key *);
int rsa_sign(char *path, struct rsa_key *rsa, unsigned char **sigret, unsigned int *siglen);
int rsa_verify(const char *path, const char *key,
		unsigned char *sig, unsigned int sig_len, int fd);

bool is_hardlink(struct hardlinks *hl, struct stat *st);

struct dns_srvinfo *
	dns_getsrvinfo(const char *zone);

int set_nameserver(const char *nsname);


#endif
