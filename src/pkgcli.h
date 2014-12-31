/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
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

#include <stdint.h>
#include <bsd_compat.h>

#define pkg_warnx(fmt, ...) pkg_fprintf(stderr, "%s" fmt, getprogname(), __VA_ARGS__, -1)

extern bool quiet;
extern int nbactions;
int nbactions;
extern int nbdone;
extern bool newpkgversion;
int nbdone;

/* pkg add */
int exec_add(int, char **);
void usage_add(void);

/* pkg annotate */
int exec_annotate(int, char **);
void usage_annotate(void);

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

/* pkg plugins */
int exec_plugins(int, char **);
void usage_plugins(void);

/* pkg lock */
int exec_lock(int, char **);
int exec_unlock(int, char **);
void usage_lock(void);

/* pkg query */
int exec_query(int, char **);
void usage_query(void);

/* pkg register */
void usage_register(void);
int exec_register(int argc, char **argv);

/* pkg repo */
int exec_repo(int, char **);
void usage_repo(void);

/* pkg rquery */
int exec_rquery(int, char **);
void usage_rquery(void);

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

/* pkg stats */
#define STATS_LOCAL (1<<0)
#define STATS_REMOTE (1<<1)

int exec_stats(int, char **);
void usage_stats(void);

/* pkg update */
int exec_update(int, char **);
void usage_update(void);
int pkgcli_update(bool, bool, const char *);

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
#define VERSION_SOURCE_INDEX	(1U<<0)
#define VERSION_ORIGIN		(1U<<1)
#define VERSION_QUIET		(1U<<2)
#define VERSION_VERBOSE		(1U<<3)
#define VERSION_STATUS		(1U<<4)
#define VERSION_NOSTATUS	(1U<<5)
#define VERSION_WITHORIGIN	(1U<<7)
#define VERSION_TESTVERSION	(1U<<8)
#define VERSION_TESTPATTERN	(1U<<9)
#define VERSION_SOURCE_PORTS	(1U<<10)
#define VERSION_SOURCE_REMOTE	(1U<<11)
#define VERSION_INDEX_FILE_NAME (1U<<12)

#define VERSION_SOURCES	(VERSION_SOURCE_PORTS | \
			 VERSION_SOURCE_INDEX | \
			 VERSION_SOURCE_REMOTE)

int exec_version(int, char **);
void usage_version(void);

/* pkg which */
int exec_which(int, char **);
void usage_which(void);

/* pkg convert */
int exec_convert(int, char **);
void usage_convert(void);

/* pkg ssh */
int exec_ssh(int, char **);
void usage_ssh(void);

/* pkg config */
int exec_config(int, char **);
void usage_config(void);

/* utils */

/* These are the fields of the Full output, in order */
#define INFO_NAME		(1LL<<0)
#define INFO_VERSION		(1LL<<1)
#define INFO_INSTALLED		(1LL<<2)
#define INFO_ORIGIN		(1LL<<3)
#define INFO_ARCH		(1LL<<4)
#define INFO_PREFIX		(1LL<<5)
#define INFO_REPOSITORY		(1LL<<6)
#define INFO_CATEGORIES		(1LL<<7)
#define INFO_LICENSES		(1LL<<8)
#define INFO_MAINTAINER		(1LL<<9)
#define INFO_WWW		(1LL<<10)
#define INFO_COMMENT		(1LL<<11)
#define INFO_OPTIONS		(1LL<<12)
#define INFO_SHLIBS_REQUIRED	(1LL<<13)
#define INFO_SHLIBS_PROVIDED	(1LL<<14)
#define INFO_ANNOTATIONS	(1LL<<15)
#define INFO_FLATSIZE		(1LL<<16)
#define INFO_PKGSIZE		(1LL<<17)
#define INFO_DESCR		(1LL<<18)

/* Other fields not part of the Full output */
#define INFO_MESSAGE		(1LL<<19)
#define INFO_DEPS		(1LL<<20)
#define INFO_RDEPS		(1LL<<21)
#define INFO_FILES		(1LL<<22)
#define INFO_DIRS		(1LL<<23)
#define INFO_USERS		(1LL<<24)
#define INFO_GROUPS		(1LL<<25)
#define INFO_REPOURL		(1LL<<26)
#define INFO_LOCKED		(1LL<<27)
#define INFO_OPTION_DEFAULTS    (1LL<<28)
#define INFO_OPTION_DESCRIPTIONS (1LL<<29)

#define INFO_LASTFIELD	INFO_LOCKED
#define INFO_ALL	(((INFO_LASTFIELD) << 1) - 1)

/* Identifying tags */
#define INFO_TAG_NAME		(1LL<<60)
#define INFO_TAG_ORIGIN		(1LL<<61)
#define INFO_TAG_NAMEVER	(1LL<<62)

/* Output YAML format */
#define INFO_RAW		(-1LL<<63)
#define INFO_RAW_YAML		(-1LL<<62)
#define INFO_RAW_JSON		(-1LL<<61)
#define INFO_RAW_JSON_COMPACT	(-1LL<<60)
#define INFO_RAW_UCL		(-1LL<<59)

/* Everything in the 'full' package output */
#define INFO_FULL	(INFO_NAME|INFO_VERSION|INFO_INSTALLED|INFO_ORIGIN| \
			 INFO_ARCH|INFO_PREFIX|INFO_REPOSITORY|		 \
			 INFO_CATEGORIES|INFO_LICENSES|INFO_MAINTAINER|	 \
			 INFO_WWW|INFO_COMMENT|INFO_OPTIONS|		 \
			 INFO_SHLIBS_REQUIRED|INFO_SHLIBS_PROVIDED|	 \
			 INFO_ANNOTATIONS|INFO_FLATSIZE|INFO_PKGSIZE|	 \
			 INFO_DESCR)

/* Everything that can take more than one line to print */
#define INFO_MULTILINE	(INFO_OPTIONS|INFO_SHLIBS_REQUIRED|	       \
			 INFO_SHLIBS_PROVIDED|INFO_ANNOTATIONS|	       \
			 INFO_DESCR|INFO_MESSAGE|INFO_DEPS|INFO_RDEPS| \
			 INFO_FILES|INFO_DIRS)

bool query_yesno(bool deft, const char *msg, ...);
int query_select(const char *msg, const char **opts, int ncnt, int deft);
bool query_tty_yesno(bool deft, const char *msg, ...);
int info_flags(uint64_t opt, bool remote);
void print_info(struct pkg * const pkg, uint64_t opt);
int print_jobs_summary(struct pkg_jobs *j, const char *msg, ...);

void job_status_begin(struct sbuf *);
void job_status_end(struct sbuf *);

int event_callback(void *data, struct pkg_event *ev);
void progressbar_start(const char *pmsg);
void progressbar_tick(int64_t current, int64_t total);
void progressbar_stop(void);

void sbuf_flush(struct sbuf *buf);

extern struct sbuf *messages;


/* pkg-query / pkg-rquery */
struct query_flags {
	const char flag;
	const char *options;
	const unsigned multiline;
	const int dbflags;
};

void print_query(struct pkg *pkg, char *qstr, char multiline);
int format_sql_condition(const char *str, struct sbuf *sqlcond,
			 bool for_remote);
int analyse_query_string(char *qstr, struct query_flags *q_flags,
			 const unsigned int q_flags_len, int *flags,
			 char *multiline);

extern int yes;
extern int dry_run;
extern int auto_update;
extern int case_sensitive;
extern int force;
extern bool quiet;
extern bool newpkgversion;
void set_globals(void);

#endif
