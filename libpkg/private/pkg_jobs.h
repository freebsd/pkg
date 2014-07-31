/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef PKG_JOBS_H_
#define PKG_JOBS_H_

#include <sys/cdefs.h>
#include <sys/sbuf.h>
#include <sys/types.h>

#include <stdbool.h>
#include <uthash.h>
#include <utlist.h>
#include <ucl.h>

#include "private/utils.h"
#include "private/pkg.h"
#include "pkg.h"

struct pkg_job_universe_item {
	struct pkg *pkg;
	struct job_pattern *jp;
	int priority;
	struct pkg *reinstall;
	UT_hash_handle hh;
	struct pkg_job_universe_item *next, *prev;
};

struct pkg_job_request {
	struct pkg_job_universe_item *item;
	bool skip;
	UT_hash_handle hh;
};

struct pkg_solved {
	struct pkg_job_universe_item *items[2];
	pkg_solved_t type;
	bool already_deleted;
	struct pkg_solved *prev, *next;
};

struct pkg_job_seen {
	struct pkg_job_universe_item *un;
	const char *digest;
	UT_hash_handle hh;
};

struct pkg_job_provide {
	struct pkg_job_universe_item *un;
	const char *provide;
	struct pkg_job_provide *next, *prev;
	UT_hash_handle hh;
};

struct pkg_job_replace {
	char *new_uid;
	char *old_uid;
	struct pkg_job_replace *next;
};

struct pkg_jobs {
	struct pkg_job_universe_item *universe;
	struct pkg_job_request	*request_add;
	struct pkg_job_request	*request_delete;
	struct pkg_solved *jobs;
	struct pkg_job_seen *seen;
	struct pkgdb	*db;
	struct pkg_job_provide *provides;
	struct pkg_job_replace *uid_replaces;
	pkg_jobs_t	 type;
	pkg_flags	 flags;
	int		 solved;
	int count;
	int total;
	int conflicts_registered;
	bool need_fetch;
	const char *reponame;
	const char *destdir;
	struct job_pattern *patterns;
};

struct job_pattern {
	char		*pattern;
	char		*path;
	match_t		match;
	bool		is_file;
	UT_hash_handle hh;
};


#endif /* PKG_JOBS_H_ */
