/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#ifndef _PKG_EVENT
#define _PKG_EVENT

void pkg_emit_error(const char *fmt, ...);
void pkg_emit_notice(const char *fmt, ...);
void pkg_emit_errno(const char *func, const char *arg);
void pkg_emit_already_installed(struct pkg *p);
void pkg_emit_fetch_begin(const char *url);
void pkg_emit_fetch_finished(const char *url);
void pkg_emit_update_add(int total, int done);
void pkg_emit_update_remove(int total, int done);
void pkg_emit_install_begin(struct pkg *p);
void pkg_emit_install_finished(struct pkg *p);
void pkg_emit_deinstall_begin(struct pkg *p);
void pkg_emit_deinstall_finished(struct pkg *p);
void pkg_emit_upgrade_begin(struct pkg *new, struct pkg *old);
void pkg_emit_upgrade_finished(struct pkg *new, struct pkg *old);
void pkg_emit_missing_dep(struct pkg *p, struct pkg_dep *d);
void pkg_emit_locked(struct pkg *p);
void pkg_emit_required(struct pkg *p, int force);
void pkg_emit_integritycheck_begin(void);
void pkg_emit_integritycheck_finished(int);
void pkg_emit_integritycheck_conflict(const char *uid, const char *path,
	struct pkg_event_conflict *conflicts);
void pkg_emit_noremotedb(const char *);
void pkg_emit_nolocaldb(void);
void pkg_emit_file_mismatch(struct pkg *pkg, struct pkg_file *f, const char *newsum);
void pkg_emit_newpkgversion(void);
void pkg_emit_developer_mode(const char *fmt, ...);
void pkg_emit_package_not_found(const char *);
void pkg_emit_incremental_update(const char *reponame, int processed);
void pkg_emit_backup(void);
void pkg_emit_restore(void);
void pkg_debug(int level, const char *fmt, ...);
int pkg_emit_sandbox_call(pkg_sandbox_cb call, int fd, void *ud);
int pkg_emit_sandbox_get_string(pkg_sandbox_cb call, void *ud, char **str, int64_t *len);

bool pkg_emit_query_yesno(bool deft, const char *msg);
int pkg_emit_query_select(const char *msg, const char **items, int ncnt, int deft);

void pkg_emit_progress_start(const char *fmt, ...);
void pkg_emit_progress_tick(int64_t current, int64_t total);

void pkg_emit_add_deps_begin(struct pkg *p);
void pkg_emit_add_deps_finished(struct pkg *p);
void pkg_emit_extract_begin(struct pkg *p);
void pkg_emit_extract_finished(struct pkg *p);
void pkg_emit_delete_files_begin(struct pkg *p);
void pkg_emit_delete_files_finished(struct pkg *p);

#endif
