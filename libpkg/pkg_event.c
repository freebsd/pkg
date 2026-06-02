/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2015 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC <license@futurecrew.ru>
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
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
#include <xstring.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

static pkg_event_cb _cb = NULL;
static void *_data = NULL;

static void
pipe_errno(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"ERROR\", "
	    "\"data\": {"
	    "\"msg\": \"%s(%s): %s\","
	    "\"errno\": %d}}",
	    json_escape(ev->e_errno.func),
	    json_escape(ev->e_errno.arg),
	    json_escape(strerror(ev->e_errno.no)),
	    ev->e_errno.no);
}

static void
pipe_error(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"ERROR\", "
	    "\"data\": {\"msg\": \"%s\"}}",
	    json_escape(ev->e_pkg_error.msg));
}

static void
pipe_notice(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"NOTICE\", "
	    "\"data\": {\"msg\": \"%s\"}}",
	    json_escape(ev->e_pkg_notice.msg));
}

static void
pipe_developer_mode(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"ERROR\", "
	    "\"data\": {\"msg\": \"DEVELOPER_MODE: %s\"}}",
	    json_escape(ev->e_pkg_error.msg));
}

static void
pipe_update_add(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_UPDATE_ADD\", "
	    "\"data\": { "
	    "\"fetched\": %d, "
	    "\"total\": %d"
	    "}}",
	    ev->e_upd_add.done,
	    ev->e_upd_add.total
	    );
}

static void
pipe_update_remove(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_UPDATE_REMOVE\", "
	    "\"data\": { "
	    "\"fetched\": %d, "
	    "\"total\": %d"
	    "}}",
	    ev->e_upd_remove.done,
	    ev->e_upd_remove.total
	    );
}

static void
pipe_fetch_begin(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_FETCH_BEGIN\", "
	    "\"data\": { "
	    "\"url\": \"%s\" "
	    "}}",
	    json_escape(ev->e_fetching.url)
	    );
}

static void
pipe_fetch_finished(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_FETCH_FINISHED\", "
	    "\"data\": { "
	    "\"url\": \"%s\" "
	    "}}",
	    json_escape(ev->e_fetching.url)
	    );
}

static void
pipe_install_begin(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"INFO_INSTALL_BEGIN\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\""
	    "}}", ev->e_install_begin.pkg, ev->e_install_begin.pkg);
}

static void
pipe_extract_begin(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"INFO_EXTRACT_BEGIN\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\""
	    "}}", ev->e_extract_begin.pkg, ev->e_extract_begin.pkg);
}

static void
pipe_extract_finished(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"INFO_EXTRACT_FINISHED\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\""
	    "}}", ev->e_extract_finished.pkg, ev->e_extract_finished.pkg);
}

static void
pipe_install_finished(struct pkg_event *ev, xstring *msg)
{
	char *msgjson = pkg_has_message(ev->e_install_finished.pkg) ?
	    pkg_message_to_str(ev->e_install_finished.pkg) : NULL;
	pkg_fprintf(msg->fp, "{ \"type\": \"INFO_INSTALL_FINISHED\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\", "
	    "\"message\": %S"
	    "}}",
	    ev->e_install_finished.pkg,
	    ev->e_install_finished.pkg,
	    msgjson != NULL ? msgjson : "\"\"");
	free(msgjson);
}

static void
pipe_integritycheck_begin(struct pkg_event *ev __unused, xstring *msg)
{
	fputs("{ \"type\": \"INFO_INTEGRITYCHECK_BEGIN\", "
	    "\"data\": {}}", msg->fp);
}

static void
pipe_integritycheck_conflict(struct pkg_event *ev, xstring *msg)
{
	struct pkg_event_conflict *cur_conflict;

	xprintf(msg, "{ \"type\": \"INFO_INTEGRITYCHECK_CONFLICT\","
		"\"data\": { "
		"\"pkguid\": \"%s\", "
		"\"pkgpath\": \"%s\", "
		"\"conflicts\": [",
		ev->e_integrity_conflict.pkg_uid,
		ev->e_integrity_conflict.pkg_path);
	cur_conflict = ev->e_integrity_conflict.conflicts;
	while (cur_conflict != NULL) {
		if (cur_conflict->next != NULL) {
			xprintf(msg, "{\"uid\":\"%s\"},",
					cur_conflict->uid);
		}
		else {
			xprintf(msg, "{\"uid\":\"%s\"}",
					cur_conflict->uid);
			break;
		}
		cur_conflict = cur_conflict->next;
	}
	xputs(msg, "]}}");
}

static void
pipe_integritycheck_finished(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_INTEGRITYCHECK_FINISHED\", "
	    "\"data\": {\"conflicting\": %d}}",
	    ev->e_integrity_finished.conflicting);
}

static void
pipe_deinstall_begin(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"INFO_DEINSTALL_BEGIN\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\""
	    "}}",
	    ev->e_deinstall_begin.pkg,
	    ev->e_deinstall_begin.pkg);
}

static void
pipe_deinstall_finished(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"INFO_DEINSTALL_FINISHED\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\""
	    "}}",
	    ev->e_deinstall_finished.pkg,
	    ev->e_deinstall_finished.pkg);
}

static void
pipe_upgrade_begin(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"INFO_UPGRADE_BEGIN\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\" ,"
	    "\"pkgnewversion\": \"%v\""
	    "}}",
	    ev->e_upgrade_begin.o,
	    ev->e_upgrade_begin.o,
	    ev->e_upgrade_begin.n);
}

static void
pipe_upgrade_finished(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"INFO_UPGRADE_FINISHED\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\" ,"
	    "\"pkgnewversion\": \"%v\""
	    "}}",
	    ev->e_upgrade_finished.o,
	    ev->e_upgrade_finished.o,
	    ev->e_upgrade_finished.n);
}

static void
pipe_locked(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"ERROR_LOCKED\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%n\""
	    "}}",
	    ev->e_locked.pkg,
	    ev->e_locked.pkg);
}

static void
pipe_required(struct pkg_event *ev, xstring *msg)
{
	struct pkg_dep *dep = NULL;
	int c = 0;

	pkg_fprintf(msg->fp, "{ \"type\": \"ERROR_REQUIRED\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\", "
	    "\"force\": %S, "
	    "\"required_by\": [",
	    ev->e_required.pkg,
	    ev->e_required.pkg,
	    ev->e_required.force == 1 ? "true": "false");
	while (pkg_rdeps(ev->e_required.pkg, &dep) == EPKG_OK)
		xprintf(msg, "{ \"pkgname\": \"%s\", "
		    "\"pkgversion\": \"%s\" }, ",
		    dep->name, dep->version);
	ungetc(c, msg->fp);
	ungetc(c, msg->fp);
	xputs(msg, "]}}");
}

static void
pipe_already_installed(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"ERROR_ALREADY_INSTALLED\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\""
	    "}}",
	    ev->e_already_installed.pkg,
	    ev->e_already_installed.pkg);
}

static void
pipe_missing_dep(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"ERROR_MISSING_DEP\", "
	    "\"data\": { "
	    "\"depname\": \"%s\", "
	    "\"depversion\": \"%s\""
	    "}}" ,
	    ev->e_missing_dep.dep->name,
	    ev->e_missing_dep.dep->version);
}

static void
pipe_noremotedb(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"ERROR_NOREMOTEDB\", "
	    "\"data\": { "
	    "\"url\": \"%s\" "
	    "}}" ,
	    ev->e_remotedb.repo);
}

static void
pipe_nolocaldb(struct pkg_event *ev __unused, xstring *msg)
{
	fputs("{ \"type\": \"ERROR_NOLOCALDB\", \"data\": {}} ",
	    msg->fp);
}

static void
pipe_newpkgversion(struct pkg_event *ev __unused, xstring *msg)
{
	fputs("{ \"type\": \"INFO_NEWPKGVERSION\", \"data\": {}} ",
	    msg->fp);
}

static void
pipe_file_mismatch(struct pkg_event *ev, xstring *msg)
{
	pkg_fprintf(msg->fp, "{ \"type\": \"ERROR_FILE_MISMATCH\", "
	    "\"data\": { "
	    "\"pkgname\": \"%n\", "
	    "\"pkgversion\": \"%v\", "
	    "\"path\": \"%S\""
	    "}}",
	    ev->e_file_mismatch.pkg,
	    ev->e_file_mismatch.pkg,
	    json_escape(ev->e_file_mismatch.file->path));
}

static void
pipe_plugin_errno(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"ERROR_PLUGIN\", "
	    "\"data\": {"
	    "\"plugin\": \"%s\", "
	    "\"msg\": \"%s(%s): %s\","
	    "\"errno\": %d"
	    "}}",
	    pkg_plugin_get(ev->e_plugin_errno.plugin, PKG_PLUGIN_NAME),
	    json_escape(ev->e_plugin_errno.func),
	    json_escape(ev->e_plugin_errno.arg),
	    json_escape(strerror(ev->e_plugin_errno.no)),
	    ev->e_plugin_errno.no);
}

static void
pipe_plugin_error(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"ERROR_PLUGIN\", "
	    "\"data\": {"
	    "\"plugin\": \"%s\", "
	    "\"msg\": \"%s\""
	    "}}",
	    pkg_plugin_get(ev->e_plugin_error.plugin, PKG_PLUGIN_NAME),
	    json_escape(ev->e_plugin_error.msg));
}

static void
pipe_plugin_info(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_PLUGIN\", "
	    "\"data\": {"
	    "\"plugin\": \"%s\", "
	    "\"msg\": \"%s\""
	    "}}",
	    pkg_plugin_get(ev->e_plugin_info.plugin, PKG_PLUGIN_NAME),
	    json_escape(ev->e_plugin_info.msg));
}

static void
pipe_incremental_update(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_INCREMENTAL_UPDATE\", "
	    "\"data\": {"
	        "\"name\": \"%s\", "
		"\"processed\": %d"
		"}}", ev->e_incremental_update.reponame,
		ev->e_incremental_update.processed);
}

static void
pipe_query_yesno(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"QUERY_YESNO\", "
	    "\"data\": {"
		"\"msg\": \"%s\","
		"\"default\": \"%d\""
		"}}", ev->e_query_yesno.msg,
		ev->e_query_yesno.deft);
}

static void
pipe_query_select(struct pkg_event *ev, xstring *msg)
{
	int i;

	xprintf(msg, "{ \"type\": \"QUERY_SELECT\", "
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
		xprintf(msg, "{ \"text\": \"%s\" },",
			ev->e_query_select.items[i]);
	}
	xprintf(msg, "{ \"text\": \"%s\" } ] }}",
		ev->e_query_select.items[i]);
}

static void
pipe_progress_start(struct pkg_event *ev __unused, xstring *msg)
{
	fputs("{ \"type\": \"INFO_PROGRESS_START\", \"data\": {}}",
	    msg->fp);
}

static void
pipe_progress_tick(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_PROGRESS_TICK\", "
	  "\"data\": { \"current\": %jd, \"total\" : %jd}}",
	  (intmax_t)ev->e_progress_tick.current,
	  (intmax_t)ev->e_progress_tick.total);
}

static void
pipe_triggers_begin(struct pkg_event *ev __unused, xstring *msg)
{
	fputs("{ \"type\": \"INFO_TRIGGERS_BEGIN\", \"data\": {}}",
	    msg->fp);
}

static void
pipe_triggers_finished(struct pkg_event *ev __unused, xstring *msg)
{
	fputs("{ \"type\": \"INFO_TRIGGERS_FINISHED\", \"data\": {}}",
	    msg->fp);
}

static void
pipe_trigger(struct pkg_event *ev, xstring *msg)
{
	xprintf(msg, "{ \"type\": \"INFO_TRIGGER\", \"data\": { "
	    "\"cleanup\": %s, \"name\": \"%s\" }}",
	    ev->e_trigger.cleanup ? "true" : "false",
	    ev->e_trigger.name);
}

typedef void (*pipe_handler_fn)(struct pkg_event *ev, xstring *msg);

static const pipe_handler_fn pipe_handlers[PKG_EVENT_LAST] = {
	[PKG_EVENT_INSTALL_BEGIN] = pipe_install_begin,
	[PKG_EVENT_INSTALL_FINISHED] = pipe_install_finished,
	[PKG_EVENT_DEINSTALL_BEGIN] = pipe_deinstall_begin,
	[PKG_EVENT_DEINSTALL_FINISHED] = pipe_deinstall_finished,
	[PKG_EVENT_UPGRADE_BEGIN] = pipe_upgrade_begin,
	[PKG_EVENT_UPGRADE_FINISHED] = pipe_upgrade_finished,
	[PKG_EVENT_EXTRACT_BEGIN] = pipe_extract_begin,
	[PKG_EVENT_EXTRACT_FINISHED] = pipe_extract_finished,
	[PKG_EVENT_FETCH_BEGIN] = pipe_fetch_begin,
	[PKG_EVENT_FETCH_FINISHED] = pipe_fetch_finished,
	[PKG_EVENT_UPDATE_ADD] = pipe_update_add,
	[PKG_EVENT_UPDATE_REMOVE] = pipe_update_remove,
	[PKG_EVENT_INTEGRITYCHECK_BEGIN] = pipe_integritycheck_begin,
	[PKG_EVENT_INTEGRITYCHECK_FINISHED] = pipe_integritycheck_finished,
	[PKG_EVENT_INTEGRITYCHECK_CONFLICT] = pipe_integritycheck_conflict,
	[PKG_EVENT_NEWPKGVERSION] = pipe_newpkgversion,
	[PKG_EVENT_NOTICE] = pipe_notice,
	[PKG_EVENT_INCREMENTAL_UPDATE] = pipe_incremental_update,
	[PKG_EVENT_QUERY_YESNO] = pipe_query_yesno,
	[PKG_EVENT_QUERY_SELECT] = pipe_query_select,
	[PKG_EVENT_PROGRESS_START] = pipe_progress_start,
	[PKG_EVENT_PROGRESS_TICK] = pipe_progress_tick,
	[PKG_EVENT_ERROR] = pipe_error,
	[PKG_EVENT_ERRNO] = pipe_errno,
	[PKG_EVENT_ALREADY_INSTALLED] = pipe_already_installed,
	[PKG_EVENT_LOCKED] = pipe_locked,
	[PKG_EVENT_REQUIRED] = pipe_required,
	[PKG_EVENT_MISSING_DEP] = pipe_missing_dep,
	[PKG_EVENT_NOREMOTEDB] = pipe_noremotedb,
	[PKG_EVENT_NOLOCALDB] = pipe_nolocaldb,
	[PKG_EVENT_FILE_MISMATCH] = pipe_file_mismatch,
	[PKG_EVENT_DEVELOPER_MODE] = pipe_developer_mode,
	[PKG_EVENT_PLUGIN_ERRNO] = pipe_plugin_errno,
	[PKG_EVENT_PLUGIN_ERROR] = pipe_plugin_error,
	[PKG_EVENT_PLUGIN_INFO] = pipe_plugin_info,
	[PKG_EVENT_TRIGGERS_BEGIN] = pipe_triggers_begin,
	[PKG_EVENT_TRIGGER] = pipe_trigger,
	[PKG_EVENT_TRIGGERS_FINISHED] = pipe_triggers_finished,
};
_Static_assert(NELEM(pipe_handlers) == PKG_EVENT_LAST,
    "pipe_handlers table size does not match pkg_event_t enum");

static void
pipeevent(struct pkg_event *ev)
{
	xstring *msg;

	if (ctx.eventpipe < 0)
		return;

	msg = xstring_new();

	if (ev->type < PKG_EVENT_LAST && pipe_handlers[ev->type] != NULL)
		pipe_handlers[ev->type](ev, msg);

	xflush(msg);
	dprintf(ctx.eventpipe, "%s\n", msg->buf);
	xstring_free(msg);
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
	free(ev.e_pkg_notice.msg);
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
pkg_emit_pkg_errno(pkg_error_t err, const char *func, const char *arg)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_PKG_ERRNO;
	ev.e_errno.func = func;
	ev.e_errno.arg = arg;
	ev.e_errno.no = err;

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
pkg_emit_install_finished(struct pkg *p, struct pkg *old)
{
	struct pkg_event ev;
	bool syslog_enabled = false;

	ev.type = PKG_EVENT_INSTALL_FINISHED;
	ev.e_install_finished.pkg = p;
	ev.e_install_finished.old = old;

	syslog_enabled = pkg_object_bool(pkg_config_get("SYSLOG"));
	if (syslog_enabled) {
		syslog(LOG_NOTICE, "%s-%s installed",
		    p->name, p->version);
	}

	pkg_emit_event(&ev);
}

void
pkg_emit_add_deps_begin(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_ADD_DEPS_BEGIN;
	ev.e_add_deps_begin.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_add_deps_finished(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_ADD_DEPS_FINISHED;
	ev.e_add_deps_finished.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_extract_begin(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_EXTRACT_BEGIN;
	ev.e_extract_begin.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_extract_finished(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_EXTRACT_FINISHED;
	ev.e_extract_finished.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_delete_files_begin(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_DELETE_FILES_BEGIN;
	ev.e_delete_files_begin.pkg = p;

	pkg_emit_event(&ev);
}

void
pkg_emit_delete_files_finished(struct pkg *p)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_DELETE_FILES_FINISHED;
	ev.e_delete_files_finished.pkg = p;

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

	ev.type = PKG_EVENT_DEINSTALL_FINISHED;
	ev.e_deinstall_finished.pkg = p;

	syslog_enabled = pkg_object_bool(pkg_config_get("SYSLOG"));
	if (syslog_enabled) {
		syslog(LOG_NOTICE, "%s-%s deinstalled",
		    p->name, p->version);
	}

	pkg_emit_event(&ev);
}

void
pkg_emit_upgrade_begin(struct pkg *new, struct pkg *old)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_UPGRADE_BEGIN;
	ev.e_upgrade_begin.n = new;
	ev.e_upgrade_begin.o = old;

	pkg_emit_event(&ev);
}

void
pkg_emit_upgrade_finished(struct pkg *new, struct pkg *old)
{
	struct pkg_event ev;
	bool syslog_enabled = false;

	ev.type = PKG_EVENT_UPGRADE_FINISHED;
	ev.e_upgrade_finished.n = new;
	ev.e_upgrade_finished.o = old;

	syslog_enabled = pkg_object_bool(pkg_config_get("SYSLOG"));
	if (syslog_enabled) {
		const char *actions[] = {
			[PKG_DOWNGRADE] = "downgraded",
			[PKG_REINSTALL] = "reinstalled",
			[PKG_UPGRADE]   = "upgraded",
		};
		pkg_change_t action;

		action = pkg_version_change_between(new, old);
		syslog(LOG_NOTICE, "%s %s: %s %s %s ",
		    new->name, actions[action],
		    old->version != NULL ? old->version : new->version,
		    old->version != NULL ? "->" : "",
		    old->version != NULL ? new->version : "");
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
pkg_emit_file_mismatch(struct pkg *pkg, struct pkg_file *f, const char *newsum)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_FILE_MISMATCH;

	ev.e_file_mismatch.pkg = pkg;
	ev.e_file_mismatch.file = f;
	ev.e_file_mismatch.newsum = newsum;

	pkg_emit_event(&ev);
}

void
pkg_emit_file_missing(struct pkg *pkg, struct pkg_file *f)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_FILE_MISSING;

	ev.e_file_missing.pkg = pkg;
	ev.e_file_missing.file = f;

	pkg_emit_event(&ev);
}

void
pkg_emit_dir_missing(struct pkg *pkg, struct pkg_dir *d)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_DIR_MISSING;

	ev.e_dir_missing.pkg = pkg;
	ev.e_dir_missing.dir = d;

	pkg_emit_event(&ev);
}

void
pkg_emit_file_meta_mismatch(struct pkg *pkg, struct pkg_file *file,
			    enum pkg_meta_attribute attrib,
			    const char *db_val, const char *fs_val)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_FILE_META_MISMATCH;

	ev.e_file_meta_mismatch.pkg = pkg;
	ev.e_file_meta_mismatch.file = file;
	ev.e_file_meta_mismatch.attrib = attrib;
	ev.e_file_meta_mismatch.db_val = db_val;
	ev.e_file_meta_mismatch.fs_val = fs_val;

	pkg_emit_event(&ev);
}

void
pkg_emit_dir_meta_mismatch(struct pkg *pkg, struct pkg_dir *dir,
			   enum pkg_meta_attribute attrib,
			   const char *db_val, const char *fs_val)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_DIR_META_MISMATCH;

	ev.e_dir_meta_mismatch.pkg = pkg;
	ev.e_dir_meta_mismatch.dir = dir;
	ev.e_dir_meta_mismatch.attrib = attrib;
	ev.e_dir_meta_mismatch.db_val = db_val;
	ev.e_dir_meta_mismatch.fs_val = fs_val;

	pkg_emit_event(&ev);
}

void
pkg_emit_file_meta_ok(struct pkg *pkg, struct pkg_file *file)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_FILE_META_OK;

	ev.e_file_ok.pkg = pkg;
	ev.e_file_ok.file = file;

	pkg_emit_event(&ev);
}

void
pkg_emit_dir_meta_ok(struct pkg *pkg, struct pkg_dir *dir)
{
	struct pkg_event ev;
	ev.type = PKG_EVENT_DIR_META_OK;

	ev.e_dir_ok.pkg = pkg;
	ev.e_dir_ok.dir = dir;

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
pkg_emit_incremental_update_begin(const char *reponame)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_INCREMENTAL_UPDATE_BEGIN;
	ev.e_incremental_update.reponame = reponame;
	ev.e_incremental_update.processed = 0;

	pkg_emit_event(&ev);
}

void
pkg_emit_incremental_update(const char *reponame, int processed)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_INCREMENTAL_UPDATE;
	ev.e_incremental_update.reponame = reponame;
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

	if (_cb == NULL)
		return (pkg_handle_sandboxed_get_string(call, str, len, ud));

	ev.type = PKG_EVENT_SANDBOX_GET_STRING;
	ev.e_sandbox_call_str.call = call;
	ev.e_sandbox_call_str.userdata = ud;
	ev.e_sandbox_call_str.result = str;
	ev.e_sandbox_call_str.len = len;

	return (pkg_emit_event(&ev));
}

int
pkg_emit_sandbox_call(pkg_sandbox_cb call, int fd, void *ud)
{
	struct pkg_event ev;

	if (_cb == NULL)
		return (pkg_handle_sandboxed_call(call, fd, ud));

	ev.type = PKG_EVENT_SANDBOX_CALL;
	ev.e_sandbox_call.call = call;
	ev.e_sandbox_call.fd = fd;
	ev.e_sandbox_call.userdata = ud;

	return (pkg_emit_event(&ev));
}

void
pkg_debug(int level, const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;

	if (ctx.debug_level < level)
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
pkg_dbg(uint64_t flags, int level, const char *fmt, ...)
{
	struct pkg_event ev;
	va_list ap;
	xstring *string_fmt;
	char *nfmt;

	if (ctx.debug_level < level)
		return;

	if ((ctx.debug_flags & (flags|PKG_DBG_ALL)) == 0)
		return;

	string_fmt = xstring_new();
	ev.type = PKG_EVENT_DEBUG;
	ev.e_debug.level = level;
	for (size_t i = 0; i < NELEM(debug_flags); i++) {
		if (flags & debug_flags[i].flag) {
			if (string_fmt->size == 0) {
				xprintf(string_fmt, "(%s", debug_flags[i].name);
				xflush(string_fmt);
			} else {
				xprintf(string_fmt, "|%s", debug_flags[i].name);
			}
		}
	}
	xprintf(string_fmt, ") %s", fmt);
	nfmt = xstring_get(string_fmt);
	va_start(ap, fmt);
	vasprintf(&ev.e_debug.msg, nfmt, ap);
	va_end(ap);

	pkg_emit_event(&ev);
	free(ev.e_debug.msg);
	free(nfmt);
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

int
pkg_emit_progress_tick(int64_t current, int64_t total)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_PROGRESS_TICK;
	ev.e_progress_tick.current = current;
	ev.e_progress_tick.total = total;

	return !!pkg_emit_event(&ev);
}

void
pkg_emit_new_action(size_t current, size_t total)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_NEW_ACTION;
	ev.e_action.total = total;
	ev.e_action.current = current;

	pkg_emit_event(&ev);
}

void
pkg_emit_message(const char *message)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_MESSAGE;
	ev.e_pkg_message.msg = message;
	pkg_emit_event(&ev);
}

void
pkg_register_cleanup_callback(void (*cleanup_cb)(void *data), void *data)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_CLEANUP_CALLBACK_REGISTER;
	ev.e_cleanup_callback.cleanup_cb = cleanup_cb;
	ev.e_cleanup_callback.data = data;
	pkg_emit_event(&ev);
}

void
pkg_unregister_cleanup_callback(void (*cleanup_cb)(void *data), void *data)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_CLEANUP_CALLBACK_UNREGISTER;
	ev.e_cleanup_callback.cleanup_cb = cleanup_cb;
	ev.e_cleanup_callback.data = data;
	pkg_emit_event(&ev);
}

void
pkg_emit_conflicts(struct pkg *p1, struct pkg *p2, const char *path)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_CONFLICTS;
	ev.e_conflicts.p1 = p1;
	ev.e_conflicts.p2 = p2;
	ev.e_conflicts.path = path;
	pkg_emit_event(&ev);
}

void
pkg_emit_triggers_begin(void)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_TRIGGERS_BEGIN;

	pkg_emit_event(&ev);
}

void
pkg_emit_triggers_finished(void)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_TRIGGERS_FINISHED;

	pkg_emit_event(&ev);
}

void
pkg_emit_trigger(const char *name, bool cleanup)
{
	struct pkg_event ev;

	ev.type = PKG_EVENT_TRIGGER;
	ev.e_trigger.name = name;
	ev.e_trigger.cleanup = cleanup;

	pkg_emit_event(&ev);
}
