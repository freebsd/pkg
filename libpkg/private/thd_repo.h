/*
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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

#ifndef _PKG_THD_REPO_H
#define _PKG_THD_REPO_H

#include <sys/queue.h>
#include <sys/types.h>
#include <pthread.h>

struct pkg_result {
	struct pkg *pkg;
	char path[MAXPATHLEN + 1];
	char cksum[SHA256_DIGEST_LENGTH * 2 + 1];
	off_t size;
	int retcode; // to pass errors
	STAILQ_ENTRY(pkg_result) next;
};

struct thd_data {
	char *root_path;

	FTS *fts;
	bool stop;
	pthread_mutex_t fts_m; // protects `fts' and `stop'

	/* results is used as a FIFO */
	STAILQ_HEAD(results, pkg_result) results;
	int thd_finished;
	pthread_mutex_t results_m; // protects `results' an `thd_finished'
	pthread_cond_t has_result; // signal that there is at least one result
};

void read_pkg_file(void *);

#endif
