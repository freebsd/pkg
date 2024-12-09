/*-
 * Copyright (c) 2020-2023 Baptiste Daroussin <bapt@FreeBSD.org>
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

#pragma once

struct fetch_item {
	const char *url;
	off_t size;
	off_t offset;
	time_t mtime;
};

struct fetcher {
	const char *scheme;
	int64_t timeout;
	int (*open)(struct pkg_repo *, struct fetch_item *);
	void (*close)(struct pkg_repo *);
	void (*cleanup)(struct pkg_repo *);
	int (*fetch)(struct pkg_repo *repo, int dest, struct fetch_item *);
};

int ssh_open(struct pkg_repo *, struct fetch_item *);
int file_open(struct pkg_repo *, struct fetch_item *);
void fh_close(struct pkg_repo *);
int tcp_open(struct pkg_repo *, struct fetch_item *);
int stdio_fetch(struct pkg_repo *, int dest, struct fetch_item *);
int curl_open(struct pkg_repo *, struct fetch_item *);
int curl_fetch(struct pkg_repo *, int dest, struct fetch_item *);
void curl_cleanup(struct pkg_repo *);
