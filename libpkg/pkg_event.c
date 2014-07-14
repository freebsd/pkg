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
	while (str != NULL && *str != '\0') {
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
	int i;
	struct pkg_dep *dep = NULL;
	struct sbuf *msg, *buf;
	const char *message;
	struct pkg_event_conflict *cur_conflict;
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
	case PKG_EVENT_UPDATE_ADD:
		sbuf_printf(msg, "{ \"type\": \"INFO_UPDATE_ADD\", "
		    "\"data\": { "
		    "\"fetched\": %d, "
		    "\"total\": %d"
		    "}}",
		    ev->e_upd_add.done,
		    ev->e_upd_add.total
		    );
		break;
	case PKG_EVENT_UPDATE_REMOVE:
		sbuf_printf(msg, "{ \"type\": \"INFO_UPDATE_REMOVE\", "
		    "\"data\": { "
		    "\"fetched\": %d, "
		    "\"total\": %d"
		    "}}",
		    ev->e_upd_remove.done,
		    ev->e_upd_remove.total
		    );
		break;
	case PKG_EVENT_FETCH_BEGIN:
		sbuf_printf(msg, "{ \"type\": \"INFO_FETCH_BEGIN\", "
		    "\"data\": { "
		    "\"url\": \"%s\" "
		    "}}",
		    sbuf_json_escape(buf, ev->e_fetching.url)
		    );
		break;
	case PKG_EVENT_FETCH_FINISHED:
		sbuf_printf(msg, "{ \"type\": \"INFO_FETCH_FINISHED\", "
		    "\"data\": { "
		    "\"url\": \"%s\" "
		    "}}",
		    sbuf_json_escape(buf, ev->e_fetching.url)
		    );
		break;
	case PKG_EVENT_INSTALL_BEGIN:
		pkg_sbuf_printf(msg, "{ \"type\": \"INFO_INSTALL_BEGIN\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\""
		    "}}", ev->e_install_begin.pkg, ev->e_install_begin.pkg);
		break;
	case PKG_EVENT_INSTALL_FINISHED:
		pkg_get(ev->e_install_finished.pkg,
		    PKG_MESSAGE, &message);

		pkg_sbuf_printf(msg, "{ \"type\": \"INFO_INSTALL_FINISHED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\", "
		    "\"message\": \"%S\""
		    "}}",
		    ev->e_install_finished.pkg,
		    ev->e_install_finished.pkg,
		    sbuf_json_escape(buf, message));
		break;
	case PKG_EVENT_INTEGRITYCHECK_BEGIN:
		sbuf_printf(msg, "{ \"type\": \"INFO_INTEGRITYCHECK_BEGIN\", "
		    "\"data\": {}}");
		break;
	case PKG_EVENT_INTEGRITYCHECK_CONFLICT:
		sbuf_printf(msg, "{ \"type\": \"INFO_INTEGRITYCHECK_CONFLICT\","
			"\"data\": { "
			"\"pkguid\": \"%s\", "
			"\"pkgpath\": \"%s\", "
			"\"conflicts\": [",
			ev->e_integrity_conflict.pkg_uid,
			ev->e_integrity_conflict.pkg_path);
		cur_conflict = ev->e_integrity_conflict.conflicts;
		while (cur_conflict != NULL) {
			if (cur_conflict->next != NULL) {
				sbuf_printf(msg, "{\"uid\":\"%s\"},",
						cur_conflict->uid);
			}
			else {
				sbuf_printf(msg, "{\"uid\":\"%s\"}",
						cur_conflict->uid);
				break;
			}
			cur_conflict = cur_conflict->next;
		}
		sbuf_cat(msg, "]}}");
		break;
	case PKG_EVENT_INTEGRITYCHECK_FINISHED:
		sbuf_printf(msg, "{ \"type\": \"INFO_INTEGRITYCHECK_FINISHED\", "
		    "\"data\": {\"conflicting\": %d}}",
		    ev->e_integrity_finished.conflicting);
		break;
	case PKG_EVENT_DEINSTALL_BEGIN:
		pkg_sbuf_printf(msg, "{ \"type\": \"INFO_DEINSTALL_BEGIN\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\""
		    "}}",
		    ev->e_deinstall_begin.pkg,
		    ev->e_deinstall_begin.pkg);
		break;
	case PKG_EVENT_DEINSTALL_FINISHED:
		pkg_sbuf_printf(msg, "{ \"type\": \"INFO_DEINSTALL_FINISHED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\""
		    "}}",
		    ev->e_deinstall_finished.pkg,
		    ev->e_deinstall_finished.pkg);
		break;
	case PKG_EVENT_UPGRADE_BEGIN:
		pkg_sbuf_printf(msg, "{ \"type\": \"INFO_UPGRADE_BEGIN\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\" ,"
		    "\"pkgnewversion\": \"%v\""
		    "}}",
		    ev->e_upgrade_begin.old,
		    ev->e_upgrade_begin.old,
		    ev->e_upgrade_begin.new);
		break;
	case PKG_EVENT_UPGRADE_FINISHED:
		pkg_sbuf_printf(msg, "{ \"type\": \"INFO_UPGRADE_FINISHED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\" ,"
		    "\"pkgnewversion\": \"%v\""
		    "}}",
		    ev->e_upgrade_begin.old,
		    ev->e_upgrade_begin.old,
		    ev->e_upgrade_begin.new);
		break;
	case PKG_EVENT_LOCKED:
		pkg_sbuf_printf(msg, "{ \"type\": \"ERROR_LOCKED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%n\""
		    "}}",
		    ev->e_locked.pkg,
		    ev->e_locked.pkg);
		break;
	case PKG_EVENT_REQUIRED:
		pkg_sbuf_printf(msg, "{ \"type\": \"ERROR_REQUIRED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\", "
		    "\"force\": %S, "
		    "\"required_by\": [",
		    ev->e_required.pkg,
		    ev->e_required.pkg,
		    ev->e_required.force == 1 ? "true": "false");
		while (pkg_rdeps(ev->e_required.pkg, &dep) == EPKG_OK)
			sbuf_printf(msg, "{ \"pkgname\": \"%s\", "
			    "\"pkgversion\": \"%s\" }, ",
			    pkg_dep_name(dep),
			    pkg_dep_version(dep));
		sbuf_setpos(msg, sbuf_len(msg) - 2);
		sbuf_cat(msg, "]}}");
		break;
	case PKG_EVENT_ALREADY_INSTALLED:
		pkg_sbuf_printf(msg, "{ \"type\": \"ERROR_ALREADY_INSTALLED\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\""
		    "}}",
		    ev->e_already_installed.pkg,
		    ev->e_already_installed.pkg);
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
		pkg_sbuf_printf(msg, "{ \"type\": \"ERROR_FILE_MISMATCH\", "
		    "\"data\": { "
		    "\"pkgname\": \"%n\", "
		    "\"pkgversion\": \"%v\", "
		    "\"path\": \"%S\""
		    "}}",
		    ev->e_file_mismatch.pkg,
		    ev->e_file_mismatch.pkg,
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
	case PKG_EVENT_INCREMENTAL_UPDATE:
		sbuf_printf(msg, "{ \"type\": \"INFO_INCREMENTAL_UPDATE\", "
		    "\"data\": {"
			"\"updated\": %d, "
			"\"removed\": %d, "
			"\"added\": %d, "
			"\"processed\": %d"
			"}}", ev->e_incremental_update.updated,
			ev->e_incremental_update.removed,
			ev->e_incremental_update.added,
			ev->e_incremental_update.processed);
		break;
	case PKG_EVENT_QUERY_YESNO:
		sbuf_printf(msg, "{\"type\": \"QUERY_YESNO\", "
		    "\"data\": {"
			"\"msg\": \"%s\","
			"\"default\": \"%d\""
			"}}", ev->e_query_yesno.msg,
			ev->e_query_yesno.deft);
		break;
	case PKG_EVENT_QUERY_SELECT:
		sbuf_printf(msg, "{\"type\": \"QUERY_SELECT\", "
		    "\"data\": {"
			"\"msg\": \"%s\","
			"\"ncnt\": \"%d\","
			"\"default\": \"%d\","
			"\"items\": ["
			, ev->e_query_select.msg,
			ev->e_query_select.ncnt,
			ev->e_query_select.deft);
		for (i = 0; i < ev->e_query_select.ncnt - 1; i++)
		{
			sbuf_printf(msg, "{ \"text\": \"%s\" },",
				ev->e_query_select.items[i]);
		}
		sbuf_printf(msg, "{ \"text\": \"%s\" } ] }}",
			ev->e_query_select.items[i]);
		break;
	case PKG_EVENT_BACKUP:
	case PKG_EVENT_RESTORE:
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

static int
pkg_emit_event(struct pkg_event *ev)
{
	int ret = 0;
	pkg_plugins_hook_run(PKG_PLUGIN_HOOK_EVENT, ev, NULL);
	if (_cb != NULL)
		ret = _cb(_data, ev);
	pipeevent(ev);
	return (ret);
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
	print_trace();
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
	print_trace();
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
pkg_emit_fetch_begin(const char *url)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_FETCH_BEGIN;
	ev.e_fetching.url = url;

	pkg_emit_event(&ev);
}

void
pkg_emit_fetch_finished(const char *url)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_FETCH_FINISHED;
	ev.e_fetching.url = url;

	pkg_emit_event(&ev);
}

void
pkg_emit_update_remove(int total, int done)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_UPDATE_REMOVE;
	ev.e_upd_remove.total = total;
	ev.e_upd_remove.done = done;

	pkg_emit_event(&ev);
}


void
pkg_emit_update_add(int total, int done)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_UPDATE_ADD;
	ev.e_upd_add.total = total;
	ev.e_upd_add.done = done;

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

	syslog_enabled = pkg_object_bool(pkg_config_get("SYSLOG"));
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
pkg_emit_integritycheck_finished(int conflicting)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_INTEGRITYCHECK_FINISHED;
	ev.e_integrity_finished.conflicting = conflicting;

	pkg_emit_event(&ev);
}

void
pkg_emit_integritycheck_conflict(const char *uid,
	const char *path, struct pkg_event_conflict *conflicts)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_INTEGRITYCHECK_CONFLICT;
	ev.e_integrity_conflict.pkg_uid = uid;
	ev.e_integrity_conflict.pkg_path = path;
	ev.e_integrity_conflict.conflicts = conflicts;

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

	syslog_enabled = pkg_object_bool(pkg_config_get("SYSLOG"));
	if (syslog_enabled) {
		pkg_get(p, PKG_NAME, &name, PKG_VERSION, &version);
		syslog(LOG_NOTICE, "%s-%s deinstalled", name, version);
	}

	pkg_emit_event(&ev);
}

void
pkg_emit_upgrade_begin(struct pkg *new, struct pkg *old)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_UPGRADE_BEGIN;
	ev.e_upgrade_begin.new = new;
	ev.e_upgrade_begin.old = old;

	pkg_emit_event(&ev);
}

void
pkg_emit_upgrade_finished(struct pkg *new, struct pkg *old)
{
	struct pkg_event ev;
	bool syslog_enabled = false;
	char *name, *oldversion, *version;

	ev.type = PKG_EVENT_UPGRADE_FINISHED;
	ev.e_upgrade_finished.new = new;
	ev.e_upgrade_finished.old = old;

	syslog_enabled = pkg_object_bool(pkg_config_get("SYSLOG"));
	if (syslog_enabled) {
		const char *actions[] = {
			[PKG_DOWNGRADE] = "downgraded",
			[PKG_REINSTALL] = "reinstalled",
			[PKG_UPGRADE]   = "upgraded",
		};
		pkg_change_t action;

		pkg_get(new, PKG_NAME, &name, PKG_VERSION, &version);
		pkg_get(old, PKG_VERSION, &oldversion);
		action = pkg_version_change_between(new, old);
		syslog(LOG_NOTICE, "%s %s: %s %s %s ",
		    name, actions[action],
		    oldversion != NULL ? oldversion : version,
		    oldversion != NULL ? "->" : "",
		    oldversion != NULL ? version : "");
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

void
pkg_emit_incremental_update(int updated, int removed, int added, int processed)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_INCREMENTAL_UPDATE;
	ev.e_incremental_update.updated = updated;
	ev.e_incremental_update.removed = removed;
	ev.e_incremental_update.added = added;
	ev.e_incremental_update.processed = processed;

	pkg_emit_event(&ev);
}

bool
pkg_emit_query_yesno(bool deft, const char *msg)
{
	struct pkg_event ev;
	int ret;

	ev.type = PKG_EVENT_QUERY_YESNO;
	ev.e_query_yesno.msg = msg;
	ev.e_query_yesno.deft = deft;

	ret = pkg_emit_event(&ev);
	return (ret ? true : false);
}

int
pkg_emit_query_select(const char *msg, const char **items, int ncnt, int deft)
{
	struct pkg_event ev;
	int ret;

	ev.type = PKG_EVENT_QUERY_SELECT;
	ev.e_query_select.msg = msg;
	ev.e_query_select.items = items;
	ev.e_query_select.ncnt = ncnt;
	ev.e_query_select.deft = deft;

	ret = pkg_emit_event(&ev);
	return ret;
}

int
pkg_emit_sandbox_get_string(pkg_sandbox_cb call, void *ud, char **str, int64_t *len)
{
	struct pkg_event ev;
	int ret;

	ev.type = PKG_EVENT_SANDBOX_GET_STRING;
	ev.e_sandbox_call_str.call = call;
	ev.e_sandbox_call_str.userdata = ud;
	ev.e_sandbox_call_str.result = str;
	ev.e_sandbox_call_str.len = len;

	ret = pkg_emit_event(&ev);
	return ret;
}

int
pkg_emit_sandbox_call(pkg_sandbox_cb call, int fd, void *ud)
{
	struct pkg_event ev;
	int ret;

	ev.type = PKG_EVENT_SANDBOX_CALL;
	ev.e_sandbox_call.call = call;
	ev.e_sandbox_call.fd = fd;
	ev.e_sandbox_call.userdata = ud;

	ret = pkg_emit_event(&ev);
	return ret;
}

void
pkg_debug(int level, const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;
	int64_t expectlevel;

	expectlevel = pkg_object_int(pkg_config_get("DEBUG_LEVEL"));

	if (expectlevel < level)
		return;

	ev.type = PKG_EVENT_DEBUG;
	ev.e_debug.level = level;
	va_start(ap, fmt);
	vasprintf(&ev.e_debug.msg, fmt, ap);
	va_end(ap);

	pkg_emit_event(&ev);
	free(ev.e_debug.msg);
}

void
pkg_emit_backup(void)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_BACKUP;

	pkg_emit_event(&ev);
}

void
pkg_emit_restore(void)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_RESTORE;

	pkg_emit_event(&ev);
}

void
pkg_emit_progress_start(const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;

	ev.type = PKG_EVENT_PROGRESS_START;
	if (fmt != NULL) {
		va_start(ap, fmt);
		vasprintf(&ev.e_progress_start.msg, fmt, ap);
		va_end(ap);
	} else {
		ev.e_progress_start.msg = NULL;
	}

	pkg_emit_event(&ev);
	free(ev.e_progress_start.msg);
}

void
pkg_emit_progress_tick(int64_t current, int64_t total)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_PROGRESS_TICK;
	ev.e_progress_tick.current = current;
	ev.e_progress_tick.total = total;

	pkg_emit_event(&ev);

}
