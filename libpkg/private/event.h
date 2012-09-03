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

#ifndef _PKG_EVENT
#define _PKG_EVENT

void pkg_emit_error(const char *fmt, ...);
void pkg_emit_errno(const char *func, const char *arg);
void pkg_emit_already_installed(struct pkg *p);
void pkg_emit_fetching(const char *url, off_t total, off_t done, time_t elapsed);
void pkg_emit_install_begin(struct pkg *p);
void pkg_emit_install_finished(struct pkg *p);
void pkg_emit_deinstall_begin(struct pkg *p);
void pkg_emit_deinstall_finished(struct pkg *p);
void pkg_emit_upgrade_begin(struct pkg *p);
void pkg_emit_upgrade_finished(struct pkg *p);
void pkg_emit_missing_dep(struct pkg *p, struct pkg_dep *d);
void pkg_emit_required(struct pkg *p, int force);
void pkg_emit_integritycheck_begin(void);
void pkg_emit_integritycheck_finished(void);
void pkg_emit_noremotedb(const char *);
void pkg_emit_nolocaldb(void);
void pkg_emit_file_mismatch(struct pkg *pkg, struct pkg_file *f, const char *newsum);
void pkg_emit_newpkgversion(void);
void pkg_emit_developer_mode(const char *fmt, ...);

#endif
