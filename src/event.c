/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2015 Matthew Seaman <matthew@FreeBSD.org>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/resource.h>
#include <sys/types.h>
#ifdef HAVE_LIBJAIL
#include <sys/sysctl.h>
#endif
#include <sys/wait.h>
#include <sys/socket.h>

#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
#endif

#include <err.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#include <bsd_compat.h>

#include "pkg.h"
#include "pkgcli.h"
#include "xmalloc.h"

#define STALL_TIME 5

xstring *messages = NULL;
xstring *conflicts = NULL;

struct cleanup {
	void *data;
	void (*cb)(void *);
};

static char *progress_message = NULL;
static xstring *msg_buf = NULL;
static int last_progress_percent = -1;
static bool progress_started = false;
static bool progress_interrupted = false;
static bool progress_debit = false;
static int64_t last_tick = 0;
static int64_t stalled;
static int64_t bytes_per_second;
static time_t last_update;
static time_t begin = 0;
static int add_deps_depth;
static vec_t(struct cleanup *) cleanup_list = vec_init();
static bool signal_handler_installed = false;
static size_t nbactions = 0;
static size_t nbdigits = 0;
static size_t nbdone = 0;

/* units for format_size */
static const char *unit_SI[] = { " ", "k", "M", "G", "T", };

static void draw_progressbar(int64_t current, int64_t total);

static void
cleanup_handler(int dummy __unused)
{
	struct cleanup *ev;

	if (cleanup_list.len == 0)
		return;
	warnx("\nsignal received, cleaning up");
	vec_foreach(cleanup_list, i) {
		ev = cleanup_list.d[i];
		ev->cb(ev->data);
	}
	exit(1);
}

static void
format_rate_SI(char *buf, int size, off_t bytes)
{
	int i;

	bytes *= 100;
	for (i = 0; bytes >= 100*1000 && unit_SI[i][0] != 'T'; i++)
		bytes = (bytes + 500) / 1000;
	if (i == 0) {
		i++;
		bytes = (bytes + 500) / 1000;
	}
	snprintf(buf, size, "%3lld.%1lld %sB",
	    (long long) (bytes + 5) / 100,
	    (long long) (bytes + 5) / 10 % 10,
	    unit_SI[i]);
}

void
job_status_end(xstring *msg)
{
	fflush(msg->fp);
	printf("%s\n", msg->buf);
	xstring_reset(msg);
}

static int
count_digits(int n){
	if (n == 0)
		return (1);
	if (n < 0)
		n = -n;
	int c = 0;
	while (n > 0) {
		n /= 10; c++;
	}
	return (c);
}


void
job_status_begin(xstring *msg)
{
	int n;

	xstring_reset(msg);
#ifdef HAVE_LIBJAIL
	static char hostname[MAXHOSTNAMELEN] = "";
	static int jailed = -1;
	size_t intlen;

	if (jailed == -1) {
		intlen = sizeof(jailed);
		if (sysctlbyname("security.jail.jailed", &jailed, &intlen,
		    NULL, 0) == -1)
			jailed = 0;
	}

	if (jailed == 1) {
		if (hostname[0] == '\0')
			gethostname(hostname, sizeof(hostname));

		fprintf(msg->fp, "[%s] ", hostname);
	}
#endif

	/* Only used for pkg-add right now. */
	if (add_deps_depth) {
		if (add_deps_depth > 1) {
			for (n = 0; n < (2 * add_deps_depth); ++n) {
				if (n % 4 == 0 && n < (2 * add_deps_depth))
					fprintf(msg->fp, "|");
				else
					fprintf(msg->fp, " ");
			}
		}
		fprintf(msg->fp, "`-- ");
	}

	if ((nbtodl > 0 || nbactions > 0) && nbdone > 0) {
		if (nbdigits == 0)
			nbdigits = count_digits(nbtodl ? nbtodl : nbactions);
		fprintf(msg->fp, "[%*zu/%zu] ", nbdigits, nbdone, (nbtodl) ? nbtodl : nbactions);
	}
	if (nbtodl > 0 && nbtodl == nbdone) {
		nbtodl = 0;
		nbdone = 0;
	}
}

void
progressbar_start(const char *pmsg)
{
	if (progress_message) {
		free(progress_message);
		progress_message = NULL;
	}

	if (quiet)
		return;
	if (pmsg != NULL)
		progress_message = xstrdup(pmsg);
	else {
		fflush(msg_buf->fp);
		progress_message = xstrdup(msg_buf->buf);
	}
	last_progress_percent = -1;
	last_tick = 0;
	begin = last_update = time(NULL);
	bytes_per_second = 0;
	stalled = 0;

	progress_started = true;
	progress_interrupted = false;
	if (!isatty(STDOUT_FILENO))
		printf("%s: ", progress_message);
	else
		printf("%s:   0%%", progress_message);
}

void
progressbar_tick(int64_t current, int64_t total)
{
	int percent;

	if (!quiet && progress_started) {
		if (isatty(STDOUT_FILENO))
			draw_progressbar(current, total);
		else {
			if (progress_interrupted) {
				printf("%s...", progress_message);
			} else if (!getenv("NO_TICK")){
				percent = (total != 0) ? (current * 100. / total) : 100;
				if (last_progress_percent / 10 < percent / 10) {
					last_progress_percent = percent;
					putchar('.');
					fflush(stdout);
				}
			}
			if (current >= total)
				progressbar_stop();
		}
	}
	progress_interrupted = false;
}

void
progressbar_stop(void)
{
	if (progress_started) {
		if (!isatty(STDOUT_FILENO))
			printf(" done");
		putchar('\n');
	}
	last_progress_percent = -1;
	progress_started = false;
	progress_interrupted = false;
}

static void
draw_progressbar(int64_t current, int64_t total)
{
	int percent;
	int64_t transferred;
	time_t elapsed = 0, now = 0;
	char buf[8];
	int64_t bytes_left;
	int cur_speed;
	int hours, minutes, seconds;
	float age_factor;

	if (!progress_started) {
		progressbar_stop();
		return;
	}

	if (progress_debit) {
		now = time(NULL);
		elapsed = (now >= last_update) ? now - last_update : 0;
	}

	percent = (total != 0) ? (current * 100. / total) : 100;

	/**
	 * Wait for interval for debit bars to keep calc per second.
	 * If not debit, show on every % change, or if ticking after
	 * an interruption (which removed our progressbar output).
	 */
	if (current >= total || (progress_debit && elapsed >= 1) ||
	    (!progress_debit &&
	    (percent != last_progress_percent || progress_interrupted))) {
		last_progress_percent = percent;

		printf("\r%s: %3d%%", progress_message, percent);
		if (progress_debit) {
			transferred = current - last_tick;
			last_tick = current;
			bytes_left = total - current;
			if (bytes_left <= 0) {
				elapsed = now - begin;
				/* Always show at least 1 second at end. */
				if (elapsed == 0)
					elapsed = 1;
				/* Calculate true total speed when done */
				transferred = total;
				bytes_per_second = 0;
			}

			if (elapsed != 0)
				cur_speed = (transferred / elapsed);
			else
				cur_speed = transferred;

#define AGE_FACTOR_SLOW_START 3
			if (now - begin <= AGE_FACTOR_SLOW_START)
				age_factor = 0.4;
			else
				age_factor = 0.9;

			if (bytes_per_second != 0) {
				bytes_per_second =
				    (bytes_per_second * age_factor) +
				    (cur_speed * (1.0 - age_factor));
			} else
				bytes_per_second = cur_speed;

			humanize_number(buf, sizeof(buf),
			    current,"B", HN_AUTOSCALE, HN_IEC_PREFIXES);
			printf(" %*s", (int)sizeof(buf), buf);

			if (bytes_left > 0)
				format_rate_SI(buf, sizeof(buf), transferred);
			else /* Show overall speed when done */
				format_rate_SI(buf, sizeof(buf),
				    bytes_per_second);
			printf(" %s/s ", buf);

			if (!transferred)
				stalled += elapsed;
			else
				stalled = 0;

			if (stalled >= STALL_TIME)
				printf(" - stalled -");
			else if (bytes_per_second == 0 && bytes_left > 0)
				printf("   --:-- ETA");
			else {
				if (bytes_left > 0)
					seconds = bytes_left / bytes_per_second;
				else
					seconds = elapsed;

				hours = seconds / 3600;
				seconds -= hours * 3600;
				minutes = seconds / 60;
				seconds -= minutes * 60;

				if (hours != 0)
					printf("%02d:%02d:%02d", hours,
					    minutes, seconds);
				else
					printf("   %02d:%02d", minutes, seconds);

				if (bytes_left > 0)
					printf(" ETA");
				else
					printf("    ");
			}
			last_update = now;
		}
		fflush(stdout);
	}
	if (current >= total)
		progressbar_stop();
}

static const char *
str_or_unknown(const char *str)
{
	if (str == NULL || str[0] == '\0')
		return "???";
	return str;
}

int
event_callback(void *data, struct pkg_event *ev)
{
	struct pkg *pkg = NULL, *pkg_new, *pkg_old;
	struct pkg_file *file;
	struct pkg_dir *dir;
	struct cleanup *evtmp;
	int *debug = data;
	struct pkg_event_conflict *cur_conflict;
	const char *filename, *reponame = NULL;

	if (msg_buf == NULL) {
		msg_buf = xstring_new();
	}

	/*
	 * If a progressbar has been interrupted by another event, then
	 * we need to add a newline to prevent bad formatting.
	 */
	if (progress_started && ev->type != PKG_EVENT_PROGRESS_TICK &&
	    !progress_interrupted) {
		putchar('\n');
		progress_interrupted = true;
	}

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
			printf("%s\n", ev->e_pkg_notice.msg);
		break;
	case PKG_EVENT_DEVELOPER_MODE:
		warnx("DEVELOPER_MODE: %s", ev->e_pkg_error.msg);
		break;
	case PKG_EVENT_UPDATE_ADD:
		if (quiet || !isatty(STDOUT_FILENO))
			break;
		printf("\rPushing new entries %d/%d", ev->e_upd_add.done, ev->e_upd_add.total);
		if (ev->e_upd_add.total == ev->e_upd_add.done)
		        putchar('\n');
		break;
	case PKG_EVENT_UPDATE_REMOVE:
		if (quiet || !isatty(STDOUT_FILENO))
			break;
		printf("\rRemoving entries %d/%d", ev->e_upd_remove.done, ev->e_upd_remove.total);
		if (ev->e_upd_remove.total == ev->e_upd_remove.done)
			putchar('\n');
		break;
	case PKG_EVENT_FETCH_BEGIN:
		if (nbtodl > 0)
			nbdone++;
		if (quiet)
			break;
		filename = strrchr(ev->e_fetching.url, '/');
		if (filename != NULL) {
			filename++;
			char *tmp = strrchr(filename, '~');
			if (tmp != NULL) {
				*tmp = '\0';
			} else {
				tmp = strrchr(filename, '.');
				if (tmp != NULL && strcmp(tmp, ".pkg") == 0)
					*tmp = '\0';
			}
		} else {
			/*
			 * We failed at being smart, so display
			 * the entire url.
			 */
			filename = ev->e_fetching.url;
		}
		job_status_begin(msg_buf);
		progress_debit = true;
		fprintf(msg_buf->fp, "Fetching %s", filename);
		break;
	case PKG_EVENT_FETCH_FINISHED:
		progress_debit = false;
		break;
	case PKG_EVENT_INSTALL_BEGIN:
		if (quiet)
			break;
		job_status_begin(msg_buf);

		pkg = ev->e_install_begin.pkg;
		pkg_fprintf(msg_buf->fp, "Installing %n-%v...\n", pkg,
		    pkg);
		fflush(msg_buf->fp);
		printf("%s", msg_buf->buf);
		break;
	case PKG_EVENT_INSTALL_FINISHED:
		if (quiet)
			break;
		break;
	case PKG_EVENT_EXTRACT_BEGIN:
		if (quiet)
			break;
		else {
			job_status_begin(msg_buf);
			pkg = ev->e_install_begin.pkg;
			pkg_fprintf(msg_buf->fp, "Extracting %n-%v", pkg, pkg);
			fflush(msg_buf->fp);
		}
		break;
	case PKG_EVENT_EXTRACT_FINISHED:
		break;
	case PKG_EVENT_ADD_DEPS_BEGIN:
		++add_deps_depth;
		break;
	case PKG_EVENT_ADD_DEPS_FINISHED:
		--add_deps_depth;
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
		if (conflicts != NULL) {
			fflush(conflicts->fp);
			printf("%s", conflicts->buf);
			xstring_free(conflicts);
			conflicts = NULL;
		}
		break;
	case PKG_EVENT_INTEGRITYCHECK_CONFLICT:
		if (*debug == 0)
			break;
		printf("\nConflict found on path %s between %s and ",
		    ev->e_integrity_conflict.pkg_path,
		    ev->e_integrity_conflict.pkg_uid);
		cur_conflict = ev->e_integrity_conflict.conflicts;
		while (cur_conflict) {
			if (cur_conflict->next)
				printf("%s, ", cur_conflict->uid);
			else
				printf("%s", cur_conflict->uid);

			cur_conflict = cur_conflict->next;
		}
		printf("\n");
		break;
	case PKG_EVENT_DEINSTALL_BEGIN:
		if (quiet)
			break;

		job_status_begin(msg_buf);

		pkg = ev->e_install_begin.pkg;
		pkg_fprintf(msg_buf->fp, "Deinstalling %n-%v...\n", pkg, pkg);
		fflush(msg_buf->fp);
		printf("%s", msg_buf->buf);
		break;
	case PKG_EVENT_DEINSTALL_FINISHED:
		if (quiet)
			break;
		break;
	case PKG_EVENT_DELETE_FILES_BEGIN:
		if (quiet)
			break;
		else {
			job_status_begin(msg_buf);
			pkg = ev->e_install_begin.pkg;
			pkg_fprintf(msg_buf->fp, "Deleting files for %n-%v",
			    pkg, pkg);
		}
		break;
	case PKG_EVENT_DELETE_FILES_FINISHED:
		break;
	case PKG_EVENT_UPGRADE_BEGIN:
		if (quiet)
			break;
		pkg_new = ev->e_upgrade_begin.n;
		pkg_old = ev->e_upgrade_begin.o;

		job_status_begin(msg_buf);

		switch (pkg_version_change_between(pkg_new, pkg_old)) {
		case PKG_DOWNGRADE:
			pkg_fprintf(msg_buf->fp, "Downgrading %n from %v to %v...\n",
			    pkg_new, pkg_old, pkg_new);
			break;
		case PKG_REINSTALL:
			pkg_fprintf(msg_buf->fp, "Reinstalling %n-%v...\n",
		    pkg_old, pkg_old);
			break;
		case PKG_UPGRADE:
			pkg_fprintf(msg_buf->fp, "Upgrading %n from %v to %v...\n",
			    pkg_new, pkg_old, pkg_new);
			break;
		}
		fflush(msg_buf->fp);
		printf("%s", msg_buf->buf);
		break;
	case PKG_EVENT_UPGRADE_FINISHED:
		if (quiet)
			break;
		break;
	case PKG_EVENT_LOCKED:
		pkg = ev->e_locked.pkg;
		pkg_printf("\n%n-%v is locked and may not be modified\n", pkg, pkg);
		break;
	case PKG_EVENT_REQUIRED:
		pkg = ev->e_required.pkg;
		pkg_printf("\n%n-%v is required by: %r%{%rn-%rv%| %}", pkg, pkg, pkg);
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
		warnx("Missing dependency '%s'",
		    pkg_dep_name(ev->e_missing_dep.dep));
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
		pkg_fprintf(stderr, "%n-%v: checksum mismatch for %Fn\n", pkg,
		    pkg, ev->e_file_mismatch.file);
		break;
	case PKG_EVENT_FILE_MISSING:
		pkg = ev->e_file_missing.pkg;
		pkg_fprintf(stderr, "%n-%v: missing file %Fn\n", pkg, pkg,
		    ev->e_file_missing.file);
		break;
	case PKG_EVENT_DIR_META_MISMATCH:
		pkg = ev->e_file_meta_mismatch.pkg;
		dir = ev->e_dir_meta_mismatch.dir;
		pkg_fprintf(stderr, "%n-%v: %Dn [%S] %S -> %S\n", pkg, pkg, dir,
			    pkg_meta_attribute_tostring(ev->e_dir_meta_mismatch.attrib),
			    str_or_unknown(ev->e_dir_meta_mismatch.db_val),
			    str_or_unknown(ev->e_dir_meta_mismatch.fs_val));
		break;
	case PKG_EVENT_FILE_META_MISMATCH:
		pkg = ev->e_file_meta_mismatch.pkg;
		file = ev->e_file_meta_mismatch.file;
		pkg_fprintf(stderr, "%n-%v: %Fn [%S] %S -> %S\n", pkg, pkg, file,
			    pkg_meta_attribute_tostring(ev->e_file_meta_mismatch.attrib),
			    str_or_unknown(ev->e_file_meta_mismatch.db_val),
			    str_or_unknown(ev->e_file_meta_mismatch.fs_val));
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
			printf("%s repository update completed. %d packages processed.\n",
			    ev->e_incremental_update.reponame,
			    ev->e_incremental_update.processed);
		break;
	case PKG_EVENT_DEBUG:
		fprintf(stderr, "DBG(%d)[%d]> %s\n", ev->e_debug.level,
			(int)getpid(), ev->e_debug.msg);
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
		return ( pkg_handle_sandboxed_call(ev->e_sandbox_call.call,
				ev->e_sandbox_call.fd,
				ev->e_sandbox_call.userdata) );
		break;
	case PKG_EVENT_SANDBOX_GET_STRING:
		return ( pkg_handle_sandboxed_get_string(ev->e_sandbox_call_str.call,
				ev->e_sandbox_call_str.result,
				ev->e_sandbox_call_str.len,
				ev->e_sandbox_call_str.userdata) );
		break;
	case PKG_EVENT_PROGRESS_START:
		progressbar_start(ev->e_progress_start.msg);
		break;
	case PKG_EVENT_PROGRESS_TICK:
		progressbar_tick(ev->e_progress_tick.current,
		    ev->e_progress_tick.total);
		break;
	case PKG_EVENT_BACKUP:
		fprintf(msg_buf->fp, "Backing up");
		break;
	case PKG_EVENT_RESTORE:
		fprintf(msg_buf->fp, "Restoring");
		break;
	case PKG_EVENT_NEW_ACTION:
		nbactions = ev->e_action.total;
		nbdigits = 0;
		nbdone = ev->e_action.current;
		break;
	case PKG_EVENT_MESSAGE:
		if (messages == NULL)
			messages = xstring_new();
		fprintf(messages->fp, "%s", ev->e_pkg_message.msg);
		break;
	case PKG_EVENT_CLEANUP_CALLBACK_REGISTER:
		if (!signal_handler_installed) {
			signal(SIGINT, cleanup_handler);
			signal_handler_installed = true;
		}
		evtmp = xmalloc(sizeof(struct cleanup));
		evtmp->cb = ev->e_cleanup_callback.cleanup_cb;
		evtmp->data = ev->e_cleanup_callback.data;
		vec_push(&cleanup_list, evtmp);
		break;
	case PKG_EVENT_CLEANUP_CALLBACK_UNREGISTER:
		if (!signal_handler_installed)
			break;
		vec_foreach(cleanup_list, i) {
			evtmp = cleanup_list.d[i];
			if (evtmp->cb == ev->e_cleanup_callback.cleanup_cb &&
			    evtmp->data == ev->e_cleanup_callback.data) {
				vec_remove_and_free(&cleanup_list, i, free);
				break;
			}
		}
		break;
	case PKG_EVENT_CONFLICTS:
		if (conflicts == NULL) {
			conflicts = xstring_new();
		}
		pkg_fprintf(conflicts->fp, "  - %n-%v",
		    ev->e_conflicts.p1, ev->e_conflicts.p1);
		if (pkg_repos_total_count() > 1) {
			pkg_get(ev->e_conflicts.p1, PKG_ATTR_REPONAME, &reponame);
			fprintf(conflicts->fp, " [%s]",
			    reponame == NULL ? "installed" : reponame);
		}
		pkg_fprintf(conflicts->fp, " conflicts with %n-%v",
		    ev->e_conflicts.p2, ev->e_conflicts.p2);
		if (pkg_repos_total_count() > 1) {
			pkg_get(ev->e_conflicts.p2, PKG_ATTR_REPONAME, &reponame);
			fprintf(conflicts->fp, " [%s]",
			    reponame == NULL ? "installed" : reponame);
		}
		fprintf(conflicts->fp, " on %s\n",
		    ev->e_conflicts.path);
		break;
	case PKG_EVENT_TRIGGER:
		if (!quiet) {
			if (ev->e_trigger.cleanup)
				printf("==> Cleaning up trigger: %s\n", ev->e_trigger.name);
			else
				printf("==> Running trigger: %s\n", ev->e_trigger.name);
		}
	default:
		break;
	}

	return 0;
}
