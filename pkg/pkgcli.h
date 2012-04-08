/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#ifndef _PKGCLI_H
#define _PKGCLI_H

extern bool quiet;

/* pkg add */
int exec_add(int, char **);
void usage_add(void);

/* pkg audit */
int exec_audit(int, char **);
void usage_audit(void);

/* pkg autoremove */
int exec_autoremove(int, char **);
void usage_autoremove(void);

/* pkg backup */
int exec_backup(int, char **);
void usage_backup(void);

/* pkg check */
int exec_check(int, char **);
void usage_check(void);

/* pkg clean */
int exec_clean(int, char **);
void usage_clean(void);

/* pkg create */
int exec_create(int, char **);
void usage_create(void);

/* pkg delete */
void usage_delete(void);
int exec_delete(int, char **);

/* pkg info */

int exec_info(int, char **);
void usage_info(void);

/* pkg install */
int exec_install(int, char **);
void usage_install(void);

/* pkg query */
int exec_query(int, char **);
void usage_query(void);

/* pkg register */
void usage_register(void);
int exec_register(int argc, char **argv);

/* pkg repo */
int exec_repo(int, char **);
void usage_repo(void);

/* pkg set */
int exec_set(int, char **);
void usage_set(void);

/* pkg search */
int exec_search(int, char **);
void usage_search(void);

/* pkg shlib */
int exec_shlib(int, char **);
void usage_shlib(void);
char *sanitize(char *, const char *, size_t);

/* pkg update */
int exec_update(int, char **);
void usage_update(void);

/* pkg updating */
int exec_updating(int, char **);
void usage_updating(void);

/* pkg upgrade */
int exec_upgrade(int, char **);
void usage_upgrade(void);

/* pkg fetch */
int exec_fetch(int, char **);
void usage_fetch(void);

/* pkg shell */
int exec_shell(int, char **);
void usage_shell(void);

/* pkg version */
#define VERSION_INDEX (1<<0)
#define VERSION_ORIGIN (1<<1)
#define VERSION_QUIET (1<<2)
#define VERSION_VERBOSE (1<<3)
#define VERSION_STATUS (1<<4)
#define VERSION_NOSTATUS (1<<5)
#define VERSION_EREGEX (1<<6)
#define VERSION_STRING (1<<7)
#define VERSION_WITHORIGIN (1<<8)
#define VERSION_TESTVERSION (1<<9)
#define VERSION_TESTPATTERN (1<<10)

int exec_version(int, char **);
void usage_version(void);

/* pkg which */
int exec_which(int, char **);
void usage_which(void);

/* utils */
#define INFO_PRINT_DEP (1<<0)
#define INFO_PRINT_RDEP (1<<1)
#define INFO_EXISTS (1<<2)
#define INFO_LIST_FILES (1<<3)
#define INFO_SIZE (1<<4)
#define INFO_QUIET (1<<5)
#define INFO_ORIGIN (1<<6)
#define INFO_ORIGIN_SEARCH (1<<7)
#define INFO_PREFIX (1<<8)
#define INFO_FULL (1<<9)
#define INFO_RAW (1<<10)
#define INFO_LIST_SHLIBS (1<<11)

bool query_yesno(const char *msg, ...);
void print_info(struct pkg * const pkg, unsigned int opt);
char *absolutepath(const char *src, char *dest, size_t dest_len);
void print_jobs_summary(struct pkg_jobs *j, pkg_jobs_t type, const char *msg, ...);

int event_callback(void *data, struct pkg_event *ev);

extern struct sbuf *messages;
#endif
