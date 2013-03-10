/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <errno.h>
#include <string.h>
#include <syslog.h>

#define _WITH_DPRINTF
#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

static pkg_event_cb _cb = NULL;
static void *_data = NULL;

static char *
sbuf_json_escape(struct sbuf *buf, const char *str)
{
	sbuf_clear(buf);
	while (*str != '\0') {
		if (*str == '"' || *str == '\\')
			sbuf_putc(buf, '\\');
		sbuf_putc(buf, *str);
		str++;
	}
	sbuf_finish(buf);

	return (sbuf_data(buf));
}

static void
pipeevent(struct pkg_event *ev)
{
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	struct sbuf *msg, *buf;
	const char *message;
	const char *name, *version, *newversion;

	if (eventpipe < 0)
		return;

	msg = sbuf_new_auto();
	buf = sbuf_new_auto();

	switch(ev->type) {
	case PKG_EVENT_ERRNO:
		sbuf_printf(msg, "{ \"type\": \"ERROR\", "
		    "\"data\": {"
		    "\"msg\": \"%s(%s): %s\","
		    "\"errno\": %d}}",
		    sbuf_json_escape(buf, ev->e_errno.func),
		    sbuf_json_escape(buf, ev->e_errno.arg),
		    sbuf_json_escape(buf, strerror(ev->e_errno.no)),
		    ev->e_errno.no);
		break;
	case PKG_EVENT_ERROR:
		sbuf_printf(msg, "{ \"type\": \"ERROR\", "
		    "\"data\": {\"msg\": \"%s\"}}",
		    sbuf_json_escape(buf, ev->e_pkg_error.msg));
		break;
	case PKG_EVENT_NOTICE:
		sbuf_printf(msg, "{ \"type\": \"NOTICE\", "
		    "\"data\": {\"msg\": \"%s\"}}",
		    sbuf_json_escape(buf, ev->e_pkg_notice.msg));
		break;
	case PKG_EVENT_DEVELOPER_MODE:
		sbuf_printf(msg, "{ \"type\": \"ERROR\", "
		    "\"data\": {\"msg\": \"DEVELOPER_MODE: %s\"}}",
		    sbuf_json_escape(buf, ev->e_pkg_error.msg));
		break;
	case PKG_EVENT_FETCHING:
		sbuf_printf(msg, "{ \"type\": \"INFO_FETCH\", "
		    "\"data\": { "
		    "\"url\": \"%s\", "
		    "\"fetched\": %" PRId64 ", "
		    "\"total\": %" PRId64
		    "}}",
		    sbuf_json_escape(buf, ev->e_fetching.url),
		    ev->e_fetching.done,
		    ev->e_fetching.total
		    );
		break;
	case PKG_EVENT_INSTALL_BEGIN:
		pkg_get(ev->e_install_begin.pkg, PKG_NAME, &name,
		    PKG_VERSION, &version);

		sbuf_printf(msg, "{ \"type\": \"INFO_INSTALL_BEGIN\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\""
		    "}}",
		    name,
		    version
		    );
		break;
	case PKG_EVENT_INSTALL_FINISHED:
		pkg_get(ev->e_install_finished.pkg,
		    PKG_MESSAGE, &message,
		    PKG_NAME, &name,
		    PKG_VERSION, &version);

		sbuf_printf(msg, "{ \"type\": \"INFO_INSTALL_FINISHED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\", "
		    "\"message\": \"%s\""
		    "}}",
		    name,
		    version,
		    sbuf_json_escape(buf, message));
		break;
	case PKG_EVENT_INTEGRITYCHECK_BEGIN:
		sbuf_printf(msg, "{ \"type\": \"INFO_INTEGRITYCHECK_BEGIN\", "
		    "\"data\": {}}");
		break;
	case PKG_EVENT_INTEGRITYCHECK_FINISHED:
		sbuf_printf(msg, "{ \"type\": \"INFO_INTEGRITYCHECK_FINISHED\", "
		    "\"data\": {}}");
		break;
	case PKG_EVENT_DEINSTALL_BEGIN:
		pkg_get(ev->e_deinstall_begin.pkg,
		    PKG_NAME, &name,
		    PKG_VERSION, &version);

		sbuf_printf(msg, "{ \"type\": \"INFO_DEINSTALL_BEGIN\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\""
		    "}}",
		    name,
		    version);
		break;
	case PKG_EVENT_DEINSTALL_FINISHED:
		pkg_get(ev->e_deinstall_finished.pkg,
		    PKG_NAME, &name,
		    PKG_VERSION, &version);

		sbuf_printf(msg, "{ \"type\": \"INFO_DEINSTALL_FINISHED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\""
		    "}}",
		    name,
		    version);
		break;
	case PKG_EVENT_UPGRADE_BEGIN:
		pkg_get(ev->e_upgrade_begin.pkg,
		    PKG_NAME, &name,
		    PKG_VERSION, &version,
		    PKG_NEWVERSION, &newversion);

		sbuf_printf(msg, "{ \"type\": \"INFO_UPGRADE_BEGIN\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\" ,"
		    "\"pkgnewversion\": \"%s\""
		    "}}",
		    name,
		    version,
		    newversion);
		break;
	case PKG_EVENT_UPGRADE_FINISHED:
		pkg_get(ev->e_upgrade_finished.pkg,
		    PKG_NAME, &name,
		    PKG_VERSION, &version,
		    PKG_NEWVERSION, &newversion);

		sbuf_printf(msg, "{ \"type\": \"INFO_UPGRADE_FINISHED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\" ,"
		    "\"pkgnewversion\": \"%s\""
		    "}}",
		    name,
		    version,
		    newversion);
		break;
	case PKG_EVENT_LOCKED:
		pkg_get(ev->e_locked.pkg,
		    PKG_NAME, &name,
		    PKG_VERSION, &version);
		sbuf_printf(msg, "{ \"type\": \"ERROR_LOCKED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\""
		    "}}",
		    name,
		    version);
		break;
	case PKG_EVENT_REQUIRED:
		pkg_get(ev->e_required.pkg,
		    PKG_NAME, &name,
		    PKG_VERSION, &version);
		sbuf_printf(msg, "{ \"type\": \"ERROR_REQUIRED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\", "
		    "\"force\": %s, "
		    "\"required_by\": [",
		    name,
		    version,
		    ev->e_required.force == 1 ? "true": "false");
		while (pkg_rdeps(pkg, &dep) == EPKG_OK)
			sbuf_printf(msg, "{ \"pkgname\": \"%s\", "
			    "\"pkgversion\": \"%s\" }, ",
			    pkg_dep_name(dep),
			    pkg_dep_version(dep));
		sbuf_setpos(msg, sbuf_len(msg) - 2);
		sbuf_cat(msg, "]}}");
		break;
	case PKG_EVENT_ALREADY_INSTALLED:
		pkg_get(ev->e_already_installed.pkg,
		    PKG_NAME, &name,
		    PKG_VERSION, &version);
		sbuf_printf(msg, "{ \"type\": \"ERROR_ALREADY_INSTALLED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\""
		    "}}",
		    name,
		    version);
		break;
	case PKG_EVENT_MISSING_DEP:
		sbuf_printf(msg, "{ \"type\": \"ERROR_MISSING_DEP\", "
		    "\"data\": { "
		    "\"depname\": \"%s\", "
		    "\"depversion\": \"%s\""
		    "}}" ,
		    pkg_dep_name(ev->e_missing_dep.dep),
		    pkg_dep_version(ev->e_missing_dep.dep));
		break;
	case PKG_EVENT_NOREMOTEDB:
		sbuf_printf(msg, "{ \"type\": \"ERROR_NOREMOTEDB\", "
		    "\"data\": { "
		    "\"url\": \"%s\" "
		    "}}" ,
		    ev->e_remotedb.repo);
		break;
	case PKG_EVENT_NOLOCALDB:
		sbuf_printf(msg, "{ \"type\": \"ERROR_NOLOCALDB\", "
		    "\"data\": {} ");
		break;
	case PKG_EVENT_NEWPKGVERSION:
		sbuf_printf(msg, "{ \"type\": \"INFO_NEWPKGVERSION\", "
		    "\"data\": {} ");
		break;
	case PKG_EVENT_FILE_MISMATCH:
		pkg_get(ev->e_file_mismatch.pkg,
		    PKG_NAME, &name,
		    PKG_VERSION, &version);
		sbuf_printf(msg, "{ \"type\": \"ERROR_FILE_MISMATCH\", "
		    "\"data\": { "
		    "\"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\", "
		    "\"path\": \"%s\""
		    "}}",
		    name,
		    version,
		    sbuf_json_escape(buf, pkg_file_path(ev->e_file_mismatch.file)));
		break;
	case PKG_EVENT_PLUGIN_ERRNO:
		sbuf_printf(msg, "{ \"type\": \"ERROR_PLUGIN\", "
		    "\"data\": {"
		    "\"plugin\": \"%s\", "
		    "\"msg\": \"%s(%s): %s\","
		    "\"errno\": %d"
		    "}}",
		    pkg_plugin_get(ev->e_plugin_errno.plugin, PKG_PLUGIN_NAME),
		    sbuf_json_escape(buf, ev->e_plugin_errno.func),
		    sbuf_json_escape(buf, ev->e_plugin_errno.arg),
		    sbuf_json_escape(buf, strerror(ev->e_plugin_errno.no)),
		    ev->e_plugin_errno.no);
		break;
	case PKG_EVENT_PLUGIN_ERROR:
		sbuf_printf(msg, "{ \"type\": \"ERROR_PLUGIN\", "
		    "\"data\": {"
		    "\"plugin\": \"%s\", "
		    "\"msg\": \"%s\""
		    "}}",
		    pkg_plugin_get(ev->e_plugin_error.plugin, PKG_PLUGIN_NAME),
		    sbuf_json_escape(buf, ev->e_plugin_error.msg));
		break;
	case PKG_EVENT_PLUGIN_INFO:
		sbuf_printf(msg, "{ \"type\": \"INFO_PLUGIN\", "
		    "\"data\": {"
		    "\"plugin\": \"%s\", "
		    "\"msg\": \"%s\""
		    "}}",
		    pkg_plugin_get(ev->e_plugin_info.plugin, PKG_PLUGIN_NAME),
		    sbuf_json_escape(buf, ev->e_plugin_info.msg));
		break;
	default:
		break;
	}
	sbuf_finish(msg);
	dprintf(eventpipe, "%s\n", sbuf_data(msg));
	sbuf_delete(msg);
	sbuf_delete(buf);
}

void
pkg_event_register(pkg_event_cb cb, void *data)
{
	_cb = cb;
	_data = data;
}

static void
pkg_emit_event(struct pkg_event *ev)
{
	pkg_plugins_hook_run(PKG_PLUGIN_HOOK_EVENT, ev, NULL);
	if (_cb != NULL)
		_cb(_data, ev);
	pipeevent(ev);
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
pkg_emit_notice(const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;

	ev.type = PKG_EVENT_NOTICE;

	va_start(ap, fmt);
	vasprintf(&ev.e_pkg_notice.msg, fmt, ap);
	va_end(ap);

	pkg_emit_event(&ev);
	free(ev.e_pkg_error.msg);
}

void
pkg_emit_developer_mode(const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;

	ev.type = PKG_EVENT_DEVELOPER_MODE;

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
	ev.e_errno.no = errno;

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
pkg_emit_locked(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_LOCKED;
	ev.e_locked.pkg = p;

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

void
pkg_plugin_errno(struct pkg_plugin *p, const char *func, const char *arg)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_PLUGIN_ERRNO;
	ev.e_plugin_errno.plugin = p;
	ev.e_plugin_errno.func = func;
	ev.e_plugin_errno.arg = arg;
	ev.e_plugin_errno.no = errno;

	pkg_emit_event(&ev);
}

void
pkg_plugin_error(struct pkg_plugin *p, const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;

	ev.type = PKG_EVENT_PLUGIN_ERROR;
	ev.e_plugin_error.plugin = p;

	va_start(ap, fmt);
	vasprintf(&ev.e_plugin_error.msg, fmt, ap);
	va_end(ap);

	pkg_emit_event(&ev);
	free(ev.e_plugin_error.msg);
}

void
pkg_plugin_info(struct pkg_plugin *p, const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;

	ev.type = PKG_EVENT_PLUGIN_INFO;
	ev.e_plugin_info.plugin = p;

	va_start(ap, fmt);
	vasprintf(&ev.e_plugin_info.msg, fmt, ap);
	va_end(ap);

	pkg_emit_event(&ev);
	free(ev.e_plugin_info.msg);
}

void
pkg_emit_package_not_found(const char *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_NOT_FOUND;
	ev.e_not_found.pkg_name = p;

	pkg_emit_event(&ev);
}

