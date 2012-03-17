/*
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

int
event_callback(void *data, struct pkg_event *ev)
{
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	const char *message;
	int *debug = data;
	(void)debug;
	const char *name, *version, *newversion;

	switch(ev->type) {
	case PKG_EVENT_ERRNO:
		warn("%s(%s)", ev->e_errno.func, ev->e_errno.arg);
		break;
	case PKG_EVENT_ERROR:
		warnx("%s", ev->e_pkg_error.msg);
		break;
	case PKG_EVENT_FETCHING:
		if (fetched == 0) {
			strlcpy(url, ev->e_fetching.url, sizeof(url));
			start_progress_meter(url, ev->e_fetching.total, &fetched);
		}
		fetched = ev->e_fetching.done;
		if (ev->e_fetching.done == ev->e_fetching.total) {
			stop_progress_meter();
			fetched = 0;
		}
		break;
	case PKG_EVENT_INSTALL_BEGIN:
		pkg_get(ev->e_install_begin.pkg, PKG_NAME, &name, PKG_VERSION, &version);
		printf("Installing %s-%s...", name, version);
		fflush(stdout);
		break;
	case PKG_EVENT_INSTALL_FINISHED:
		printf(" done\n");
		pkg_get(ev->e_install_finished.pkg, PKG_MESSAGE, &message);
		if (message != NULL && message[0] != '\0') {
			if (messages == NULL)
				messages = sbuf_new_auto();
			sbuf_printf(messages, "%s\n", message);
		}
		break;
	case PKG_EVENT_INTEGRITYCHECK_BEGIN:
		printf("Checking integrity...");
		fflush(stdout);
		break;
	case PKG_EVENT_INTEGRITYCHECK_FINISHED:
		printf(" done\n");
		break;
	case PKG_EVENT_DEINSTALL_BEGIN:
		pkg_get(ev->e_deinstall_begin.pkg, PKG_NAME, &name, PKG_VERSION, &version);
		printf("Deinstalling %s-%s...", name, version);
		fflush(stdout);
		break;
	case PKG_EVENT_DEINSTALL_FINISHED:
		printf(" done\n");
		break;
	case PKG_EVENT_UPGRADE_BEGIN:
		pkg_get(ev->e_upgrade_finished.pkg, PKG_NAME, &name, PKG_VERSION, &version,
		    PKG_NEWVERSION, &newversion);
		switch (pkg_version_cmp(version, newversion)) {
			case 1:
				printf("Downgrading %s from %s to %s...", name, version, newversion);
				break;
			case 0:
				printf("Reinstalling %s-%s", name, version);
				break;
			case -1:
				printf("Upgrading %s from %s to %s...", name, version, newversion);
				break;
		}
		fflush(stdout);
		break;
	case PKG_EVENT_UPGRADE_FINISHED:
		printf(" done\n");
		break;
	case PKG_EVENT_REQUIRED:
		pkg = ev->e_required.pkg;
		pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version);
		fprintf(stderr, "%s-%s is required by:", name, version);
		while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
			fprintf(stderr, " %s", pkg_dep_get(dep, PKG_DEP_ORIGIN));
		}
		if (ev->e_required.force == 1)
			fprintf(stderr, ", deleting anyway\n");
		else
			fprintf(stderr, "\n");
		break;
	case PKG_EVENT_ALREADY_INSTALLED:
		pkg_get(ev->e_already_installed.pkg, PKG_NAME, &name, PKG_VERSION, &version);
		printf("%s-%s already installed\n", name, version);
		break;
	case PKG_EVENT_MISSING_DEP:
		fprintf(stderr, "missing dependency %s-%s", pkg_dep_get(ev->e_missing_dep.dep, PKG_DEP_NAME),
		    pkg_dep_get(ev->e_missing_dep.dep, PKG_DEP_VERSION));
		break;
	case PKG_EVENT_NOREMOTEDB:
		fprintf(stderr, "Unable to open remote database \"%s\", try running `%s update` first\n", ev->e_remotedb.repo, getprogname());
		break;
	case PKG_EVENT_NOLOCALDB:
		/* only cares if run as root */
		if (geteuid() == 0)
			fprintf(stderr, "Unable to create local database\n");
		break;
	case PKG_EVENT_FILE_MISMATCH:
		pkg_get(ev->e_file_mismatch.pkg, PKG_NAME, &name, PKG_VERSION, &version);
		fprintf(stderr, "%s-%s: checksum mismatch for %s\n", name, version, pkg_file_get(ev->e_file_mismatch.file, PKG_FILE_PATH));
	default:
		break;
	}

	return 0;
}
