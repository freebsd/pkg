#include <sys/param.h>
#include <string.h>
#include <err.h>
#include <syslog.h>
#include <stdarg.h>

#include "pkg.h"
#include "progressmeter.h"
#include "event.h"

static off_t fetched = 0;
static char url[MAXPATHLEN+1];

int
event_callback(void *data, struct pkg_event *ev)
{
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	const char *message;
	int *debug = data;
	(void)debug;
	const char *name, *version, *newversion;
	bool syslog_enabled;

	pkg_config_bool(PKG_CONFIG_SYSLOG, &syslog_enabled);

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
		pkg_get(ev->e_install_finished.pkg, PKG_MESSAGE, &message,
			PKG_NAME, &name, PKG_VERSION, &version);
		if (message != NULL && message[0] != '\0')
			printf("%s\n", message);
		if (syslog_enabled)
			syslog(LOG_NOTICE, "Installed: %s-%s", name, version);
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
		if (syslog_enabled) {
			pkg_get(ev->e_deinstall_finished.pkg, PKG_NAME, &name,
				PKG_VERSION, &version);
			syslog(LOG_NOTICE, "Deinstalled: %s-%s", name, version);
		}
		break;
	case PKG_EVENT_UPGRADE_BEGIN:
		pkg_get(ev->e_upgrade_finished.pkg, PKG_NAME, &name, PKG_VERSION, &version,
		    PKG_NEWVERSION, &newversion);
		printf("Upgrading %s from %s to %s...", name, version, newversion);
		fflush(stdout);
		break;
	case PKG_EVENT_UPGRADE_FINISHED:
		printf(" done\n");
		if (syslog_enabled) {
			pkg_get(ev->e_upgrade_finished.pkg, PKG_NAME, &name,
				PKG_VERSION, &version, PKG_NEWVERSION, &newversion);
			syslog(LOG_NOTICE, "Upgrading: %s-%s to %s-%s",
				name, version, name, newversion);
		}
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
	default:
		break;
	}

	return 0;
}
