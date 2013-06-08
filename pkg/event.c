/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

#include <err.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "progressmeter.h"
#include "pkgcli.h"

static off_t fetched = 0;
static char url[MAXPATHLEN+1];
struct sbuf *messages = NULL;

static void
print_and_set_term_title(struct sbuf *msg)
{
	sbuf_finish(msg);
	printf("%s", sbuf_data(msg));
	printf("\033]0; %s\007", sbuf_data(msg));
	sbuf_delete(msg);
}

int
event_callback(void *data, struct pkg_event *ev)
{
	struct pkg *pkg = NULL;
	int *debug = data;
	(void) debug;
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

			if (nbactions > 0)
				sbuf_printf(msg, "[%d/%d] ", nbdone, nbactions);

			pkg = ev->e_install_begin.pkg;
			pkg_sbuf_printf(msg, "Installing %n-%v...", pkg, pkg);

			print_and_set_term_title(msg);
		}
		break;
	case PKG_EVENT_INSTALL_FINISHED:
		if (quiet)
			break;
		printf(" done\n");
		if (pkg_has_message(ev->e_install_finished.pkg)) {
			if (messages == NULL)
				messages = sbuf_new_auto();
			pkg_sbuf_printf(messages, "%M\n",
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
		printf(" done\n");
		break;
	case PKG_EVENT_INTEGRITYCHECK_CONFLICT:
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

			if (nbactions > 0)
				sbuf_printf(msg, "[%d/%d] ", nbdone, nbactions);

			pkg = ev->e_install_begin.pkg;
			pkg_sbuf_printf(msg, "Deleting %n-%v...", pkg, pkg);

			print_and_set_term_title(msg);
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

			pkg = ev->e_upgrade_begin.pkg;
			nbdone++;

			msg = sbuf_new_auto();
			if (msg == NULL) {
				warn("sbuf_new_auto() failed");
				break;
			}

			if (nbactions > 0)
				sbuf_printf(msg, "[%d/%d] ", nbdone, nbactions);
			switch (pkg_version_change(pkg)) {
			case PKG_DOWNGRADE:
				pkg_sbuf_printf(msg,
				    "Downgrading %n from %V to %v...",
				    pkg, pkg, pkg);
				break;
			case PKG_REINSTALL:
				pkg_sbuf_printf(msg, "Reinstalling %n-%V",
				    pkg, pkg);
				break;
			case PKG_UPGRADE:
				pkg_sbuf_printf(msg,
				    "Upgrading %n from %V to %v...",
						pkg, pkg, pkg);
				break;
			}
			print_and_set_term_title(msg);
		}
		break;
	case PKG_EVENT_UPGRADE_FINISHED:
		if (quiet)
			break;
		printf(" done\n");
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
		pkg_printf("%n-%v already installed\n", pkg, pkg);
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
		printf("New version of pkg detected; it needs to be "
		    "installed first.\nAfter this upgrade it is recommended "
		    "that you do a full upgrade using: 'pkg upgrade'\n\n");
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
	default:
		break;
	}

	return 0;
}
