/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef HAVE_CAPSICUM
#include <sys/capability.h>
#endif

#include <err.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sysexits.h>

#include "pkg.h"
#include "progressmeter.h"
#include "pkgcli.h"

static off_t fetched = 0;
static char url[MAXPATHLEN];
struct sbuf *messages = NULL;

static void
print_status_end(struct sbuf *msg)
{
	sbuf_finish(msg);
	printf("%s", sbuf_data(msg));
	/*printf("\033]0; %s\007", sbuf_data(msg));*/
	sbuf_delete(msg);
}

static void
print_status_begin(struct sbuf *msg)
{

	if (nbactions > 0)
		sbuf_printf(msg, "[%d/%d] ", nbdone, nbactions);
}

static int
event_sandboxed_call(pkg_sandbox_cb func, int fd, void *ud)
{
	pid_t pid;
	int	status, ret;

	pid = fork();

	switch(pid) {
	case -1:
		warn("fork failed");
		return (EPKG_FATAL);
		break;
	case 0:
		break;
	default:
		/* Parent process */
		while (waitpid(pid, &status, 0) == -1) {
			if (errno != EINTR) {
				warn("Sandboxed process pid=%d", (int)pid);
				ret = -1;
				break;
			}
		}

		if (WIFEXITED(status)) {
			ret = WEXITSTATUS(status);
		}
		if (WIFSIGNALED(status)) {
			/* Process got some terminating signal, hence stop the loop */
			fprintf(stderr, "Sandboxed process pid=%d terminated abnormally by signal: %d\n",
					(int)pid, WTERMSIG(status));
			ret = -1;
		}
		return (ret);
	}

	/* Here comes child process */
#ifdef HAVE_CAPSICUM
	cap_rights_init(&rights, CAP_READ|CAP_MMAP_R|CAP_SEEK|CAP_LOOKUP|CAP_FSTAT);
	if (cap_rights_limit(fileno(fd), &rights) < 0 && errno != ENOSYS ) {
		warn("cap_rights_limit() failed");
		return (EPKG_FATAL);
	}

	if (cap_enter() < 0 && errno != ENOSYS) {
		warn("cap_enter() failed");
		return (EPKG_FATAL);
	}
#endif

	/*
	 * XXX: if capsicum is not enabled we basically have no idea of how to
	 * make a sandbox
	 */
	ret = func(fd, ud);

	_exit(ret);
}

int
event_callback(void *data, struct pkg_event *ev)
{
	struct pkg *pkg = NULL, *pkg_new, *pkg_old;
	int *debug = data;
	const char *filename;
	struct pkg_event_conflict *cur_conflict;

	switch(ev->type) {
	case PKG_EVENT_ERRNO:
		warnx("%s(%s): %s", ev->e_errno.func, ev->e_errno.arg,
		    strerror(ev->e_errno.no));
		break;
	case PKG_EVENT_ERROR:
		warnx("%s", ev->e_pkg_error.msg);
		break;
	case PKG_EVENT_NOTICE:
		if (!quiet)
			warnx("%s", ev->e_pkg_notice.msg);
		break;
	case PKG_EVENT_DEVELOPER_MODE:
		warnx("DEVELOPER_MODE: %s", ev->e_pkg_error.msg);
		break;
	case PKG_EVENT_UPDATE_ADD:
		if (quiet || !isatty(fileno(stdin)))
			break;
		printf("\rPushing new entries %d/%d", ev->e_upd_add.done, ev->e_upd_add.total);
		if (ev->e_upd_add.total == ev->e_upd_add.done)
			printf("\n");
		break;
	case PKG_EVENT_UPDATE_REMOVE:
		if (quiet || !isatty(fileno(stdin)))
			break;
		printf("\rRemoving entries %d/%d", ev->e_upd_remove.done, ev->e_upd_remove.total);
		if (ev->e_upd_remove.total == ev->e_upd_remove.done)
			printf("\n");
		break;
	case PKG_EVENT_FETCHING:
		if (quiet || !isatty(fileno(stdin)))
			break;
		if (fetched == 0) {
			filename = strrchr(ev->e_fetching.url, '/');
			if (filename != NULL) {
				filename++;
			} else {
				/*
				 * We failed at being smart, so display
				 * the entire url.
				 */
				filename = ev->e_fetching.url;
			}
			strlcpy(url, filename, sizeof(url));
			start_progress_meter(url, ev->e_fetching.total,
			    &fetched);
		}
		fetched = ev->e_fetching.done;
		if (ev->e_fetching.done == ev->e_fetching.total) {
			stop_progress_meter();
			fetched = 0;
		}
		break;
	case PKG_EVENT_INSTALL_BEGIN:
		if (quiet)
			break;
		else {
			struct sbuf	*msg;

			nbdone++;

			msg = sbuf_new_auto();
			if (msg == NULL) {
				warn("sbuf_new_auto() failed");
				break;
			}

			print_status_begin(msg);

			pkg = ev->e_install_begin.pkg;
			pkg_sbuf_printf(msg, "Installing %n-%v...", pkg, pkg);

			print_status_end(msg);
		}
		break;
	case PKG_EVENT_INSTALL_FINISHED:
		if (quiet)
			break;
		printf(" done\n");
		if (pkg_has_message(ev->e_install_finished.pkg)) {
			if (messages == NULL)
				messages = sbuf_new_auto();
			pkg_sbuf_printf(messages, "Message for %n-%v:\n %M\n",
			    ev->e_install_finished.pkg,
			    ev->e_install_finished.pkg,
			    ev->e_install_finished.pkg);
		}
		break;
	case PKG_EVENT_INTEGRITYCHECK_BEGIN:
		if (quiet)
			break;
		printf("Checking integrity...");
		break;
	case PKG_EVENT_INTEGRITYCHECK_FINISHED:
		if (quiet)
			break;
		printf(" done (%d conflicting)\n", ev->e_integrity_finished.conflicting);
		break;
	case PKG_EVENT_INTEGRITYCHECK_CONFLICT:
		if (*debug == 0)
			break;
		printf("\nConflict found on path %s between %s-%s(%s) and ",
		    ev->e_integrity_conflict.pkg_path,
		    ev->e_integrity_conflict.pkg_name,
		    ev->e_integrity_conflict.pkg_version,
		    ev->e_integrity_conflict.pkg_origin);
		cur_conflict = ev->e_integrity_conflict.conflicts;
		while (cur_conflict) {
			if (cur_conflict->next)
				printf("%s-%s(%s), ", cur_conflict->name,
				    cur_conflict->version,
				    cur_conflict->origin);
			else
				printf("%s-%s(%s)", cur_conflict->name,
				    cur_conflict->version,
				    cur_conflict->origin);

			cur_conflict = cur_conflict->next;
		}
		printf("\n");
		break;
	case PKG_EVENT_DEINSTALL_BEGIN:
		if (quiet)
			break;
		else {
			struct sbuf	*msg;

			nbdone++;

			msg = sbuf_new_auto();
			if (msg == NULL) {
				warn("sbuf_new_auto() failed");
				break;
			}

			print_status_begin(msg);

			pkg = ev->e_install_begin.pkg;
			pkg_sbuf_printf(msg, "Deleting %n-%v...", pkg, pkg);

			print_status_end(msg);
		}
		break;
	case PKG_EVENT_DEINSTALL_FINISHED:
		if (quiet)
			break;
		printf(" done\n");
		break;
	case PKG_EVENT_UPGRADE_BEGIN:
		if (quiet)
			break;
		else {
			struct sbuf	*msg;

			pkg_new = ev->e_upgrade_begin.new;
			pkg_old = ev->e_upgrade_begin.old;
			nbdone++;

			msg = sbuf_new_auto();
			if (msg == NULL) {
				warn("sbuf_new_auto() failed");
				break;
			}

			print_status_begin(msg);

			switch (pkg_version_change_between(pkg_new, pkg_old)) {
			case PKG_DOWNGRADE:
				pkg_sbuf_printf(msg,
				    "Downgrading %n from %v to %v...",
				    pkg_new, pkg_new, pkg_old);
				break;
			case PKG_REINSTALL:
				pkg_sbuf_printf(msg, "Reinstalling %n-%v...",
				    pkg_old, pkg_old);
				break;
			case PKG_UPGRADE:
				pkg_sbuf_printf(msg,
				    "Upgrading %n from %v to %v...",
						pkg_old, pkg_old, pkg_new);
				break;
			}
			print_status_end(msg);
		}
		break;
	case PKG_EVENT_UPGRADE_FINISHED:
		if (quiet)
			break;
		printf(" done\n");
		pkg_new = ev->e_upgrade_begin.new;
		if (pkg_has_message(pkg_new)) {
			if (messages == NULL)
				messages = sbuf_new_auto();
			pkg_sbuf_printf(messages, "Message for %n-%v:\n %M\n",
				pkg_new, pkg_new, pkg_new);
		}
		break;
	case PKG_EVENT_LOCKED:
		pkg = ev->e_locked.pkg;
		pkg_fprintf(stderr,
		    "\n%n-%v is locked and may not be modified\n",
		    pkg, pkg);
		break;
	case PKG_EVENT_REQUIRED:
		pkg = ev->e_required.pkg;
		pkg_fprintf(stderr,
		    "\n%n-%v is required by: %r%{%rn-%rv%| %}",
		    pkg, pkg, pkg);
		if (ev->e_required.force == 1)
			fprintf(stderr, ", deleting anyway\n");
		else
			fprintf(stderr, "\n");
		break;
	case PKG_EVENT_ALREADY_INSTALLED:
		if (quiet)
			break;
		pkg = ev->e_already_installed.pkg;
		pkg_printf("the most recent version of %n-%v is already installed\n",
				pkg, pkg);
		break;
	case PKG_EVENT_NOT_FOUND:
		printf("Package '%s' was not found in "
		    "the repositories\n", ev->e_not_found.pkg_name);
		break;
	case PKG_EVENT_MISSING_DEP:
		fprintf(stderr, "missing dependency %s-%s\n",
		    pkg_dep_name(ev->e_missing_dep.dep),
		    pkg_dep_version(ev->e_missing_dep.dep));
		break;
	case PKG_EVENT_NOREMOTEDB:
		fprintf(stderr, "Unable to open remote database \"%s\". "
		    "Try running '%s update' first.\n", ev->e_remotedb.repo,
		    getprogname());
		break;
	case PKG_EVENT_NOLOCALDB:
		fprintf(stderr, "Local package database nonexistent!\n");
		break;
	case PKG_EVENT_NEWPKGVERSION:
		newpkgversion = true;
		printf("New version of pkg detected; it needs to be "
		    "installed first.\n");
		break;
	case PKG_EVENT_FILE_MISMATCH:
		pkg = ev->e_file_mismatch.pkg;
		pkg_fprintf(stderr, "%n-%v: checksum mismatch for %S\n", pkg,
		    pkg, pkg_file_path(ev->e_file_mismatch.file));
		break;
	case PKG_EVENT_PLUGIN_ERRNO:
		warnx("%s: %s(%s): %s",
		    pkg_plugin_get(ev->e_plugin_errno.plugin, PKG_PLUGIN_NAME),
		    ev->e_plugin_errno.func, ev->e_plugin_errno.arg,
		    strerror(ev->e_plugin_errno.no));
		break;
	case PKG_EVENT_PLUGIN_ERROR:
		warnx("%s: %s",
		    pkg_plugin_get(ev->e_plugin_error.plugin, PKG_PLUGIN_NAME),
		    ev->e_plugin_error.msg);
		break;
	case PKG_EVENT_PLUGIN_INFO:
		if (quiet)
			break;
		printf("%s: %s\n",
		    pkg_plugin_get(ev->e_plugin_info.plugin, PKG_PLUGIN_NAME),
		    ev->e_plugin_info.msg);
		break;
	case PKG_EVENT_INCREMENTAL_UPDATE:
		if (!quiet)
			printf("Incremental update completed, %d packages "
			    "processed:\n"
			    "%d packages updated, %d removed and %d added.\n",
			    ev->e_incremental_update.processed,
			    ev->e_incremental_update.updated,
			    ev->e_incremental_update.removed,
			    ev->e_incremental_update.added);
		break;
	case PKG_EVENT_DEBUG:
		fprintf(stderr, "DBG(%d)> %s\n", ev->e_debug.level, ev->e_debug.msg);
		break;
	case PKG_EVENT_QUERY_YESNO:
		return ( ev->e_query_yesno.deft ?
			query_yesno(true, ev->e_query_yesno.msg, "[Y/n]") :
			query_yesno(false, ev->e_query_yesno.msg, "[y/N]") );
		break;
	case PKG_EVENT_QUERY_SELECT:
		return query_select(ev->e_query_select.msg, ev->e_query_select.items,
			ev->e_query_select.ncnt, ev->e_query_select.deft);
		break;
	case PKG_EVENT_SANDBOX_CALL:
		return ( event_sandboxed_call(ev->e_sandbox_call.call,
				ev->e_sandbox_call.fd,
				ev->e_sandbox_call.userdata) );
	default:
		break;
	}

	return 0;
}
