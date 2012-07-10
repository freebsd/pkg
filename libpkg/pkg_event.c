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

#include <syslog.h>

#include "pkg.h"
#include "private/event.h"

static pkg_event_cb _cb = NULL;
static void *_data = NULL;

void
pkg_event_register(pkg_event_cb cb, void *data)
{
	_cb = cb;
	_data = data;
}

static void
pkg_emit_event(struct pkg_event *ev)
{
	if (_cb != NULL)
		_cb(_data, ev);
}

void
pkg_emit_error(const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;

	ev.type = PKG_EVENT_ERROR;

	va_start(ap, fmt);
	vasprintf(&ev.e_pkg_error.msg, fmt, ap);
	va_end(ap);

	pkg_emit_event(&ev);
	free(ev.e_pkg_error.msg);
}

void
pkg_emit_errno(const char *func, const char *arg)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_ERRNO;
	ev.e_errno.func = func;
	ev.e_errno.arg = arg;

	pkg_emit_event(&ev);
}

void
pkg_emit_already_installed(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_ALREADY_INSTALLED;
	ev.e_already_installed.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_fetching(const char *url, off_t total, off_t done, time_t elapsed)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_FETCHING;
	ev.e_fetching.url = url;
        ev.e_fetching.total = total;
        ev.e_fetching.done = done;
        ev.e_fetching.elapsed = elapsed;

	pkg_emit_event(&ev);
}

void
pkg_emit_install_begin(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_INSTALL_BEGIN;
	ev.e_install_begin.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_install_finished(struct pkg *p)
{
	struct pkg_event ev;
	bool syslog_enabled = false;
	char *name, *version;

	ev.type = PKG_EVENT_INSTALL_FINISHED;
	ev.e_install_finished.pkg = p;

	pkg_config_bool(PKG_CONFIG_SYSLOG, &syslog_enabled);
	if (syslog_enabled) {
		pkg_get(p, PKG_NAME, &name, PKG_VERSION, &version);
		syslog(LOG_NOTICE, "%s-%s installed", name, version);
	}

	pkg_emit_event(&ev);
}

void
pkg_emit_integritycheck_begin(void)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_INTEGRITYCHECK_BEGIN;

	pkg_emit_event(&ev);
}

void
pkg_emit_integritycheck_finished(void)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_INTEGRITYCHECK_FINISHED;

	pkg_emit_event(&ev);
}

void
pkg_emit_deinstall_begin(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_DEINSTALL_BEGIN;
	ev.e_deinstall_begin.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_deinstall_finished(struct pkg *p)
{
	struct pkg_event ev;
	bool syslog_enabled = false;
	char *name, *version;

	ev.type = PKG_EVENT_DEINSTALL_FINISHED;
	ev.e_deinstall_finished.pkg = p;

	pkg_config_bool(PKG_CONFIG_SYSLOG, &syslog_enabled);
	if (syslog_enabled) {
		pkg_get(p, PKG_NAME, &name, PKG_VERSION, &version);
		syslog(LOG_NOTICE, "%s-%s deinstalled", name, version);
	}

	pkg_emit_event(&ev);
}

void
pkg_emit_upgrade_begin(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_UPGRADE_BEGIN;
	ev.e_upgrade_begin.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_upgrade_finished(struct pkg *p)
{
	struct pkg_event ev;
	bool syslog_enabled = false;
	char *name, *version, *newversion;

	ev.type = PKG_EVENT_UPGRADE_FINISHED;
	ev.e_upgrade_finished.pkg = p;

	pkg_config_bool(PKG_CONFIG_SYSLOG, &syslog_enabled);
	if (syslog_enabled) {
		const char *actions[] = {
		    "upgraded", "reinstalled", "downgraded"
		};
		int num_actions = sizeof(actions) / sizeof(*actions);
		int action;

		pkg_get(p, PKG_NAME, &name, PKG_VERSION, &version,
		    PKG_NEWVERSION, &newversion);
		action = pkg_version_cmp(version, newversion) + 1;
		if (action >= 0 && action < num_actions)
			syslog(LOG_NOTICE, "%s %s: %s -> %s ",
			    name, actions[action], version, newversion);
	}

	pkg_emit_event(&ev);
}

void
pkg_emit_missing_dep(struct pkg *p, struct pkg_dep *d)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_MISSING_DEP;
	ev.e_missing_dep.pkg = p;
	ev.e_missing_dep.dep = d;

	pkg_emit_event(&ev);
}

void
pkg_emit_required(struct pkg *p, int force)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_REQUIRED;
	ev.e_required.pkg = p;
	ev.e_required.force = force;

	pkg_emit_event(&ev);
}

void
pkg_emit_nolocaldb(void)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_NOLOCALDB;

	pkg_emit_event(&ev);
}

void
pkg_emit_noremotedb(const char *repo)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_NOREMOTEDB;

	ev.e_remotedb.repo = repo;

	pkg_emit_event(&ev);
}

void
pkg_emit_newpkgversion(void)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_NEWPKGVERSION;

	pkg_emit_event(&ev);
}

void
pkg_emit_file_mismatch(struct pkg *pkg, struct pkg_file *f, const char *newsum) {
	struct pkg_event ev;
	ev.type = PKG_EVENT_FILE_MISMATCH;

	ev.e_file_mismatch.pkg = pkg;
	ev.e_file_mismatch.file = f;
	ev.e_file_mismatch.newsum = newsum;

	pkg_emit_event(&ev);
}
