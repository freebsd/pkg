#include <err.h>

#include "pkg.h"
#include "event.h"

int
event_callback(void *data __unused, struct pkg_event *ev)
{
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	unsigned int percent;
	const char *message;

	switch(ev->type) {
	case PKG_EVENT_ERRNO:
		warn("%s(%s)", ev->e_errno.func, ev->e_errno.arg);
		break;
	case PKG_EVENT_ERROR:
		warnx("%s", ev->e_pkg_error.msg);
		break;
	case PKG_EVENT_FETCHING:
		percent = ((float)ev->e_fetching.done / (float)ev->e_fetching.total) * 100;
		printf("\rFetching %s... %d%%", ev->e_fetching.url, percent);
		if (ev->e_fetching.done == ev->e_fetching.total)
			printf("\n");
		fflush(stdout);
		break;
	case PKG_EVENT_INSTALL_BEGIN:
		printf("Installing %s-%s...",
			   pkg_get(ev->e_install_begin.pkg, PKG_NAME),
			   pkg_get(ev->e_install_begin.pkg, PKG_VERSION));
		break;
	case PKG_EVENT_INSTALL_FINISHED:
		printf(" done\n");
		message = pkg_get(ev->e_install_finished.pkg, PKG_MESSAGE);
		if (message != NULL && message[0] != '\0')
			printf("%s\n", message);
		break;
	case PKG_EVENT_DEINSTALL_BEGIN:
		printf("Deinstalling %s-%s...",
			   pkg_get(ev->e_deinstall_begin.pkg, PKG_NAME),
			   pkg_get(ev->e_deinstall_begin.pkg, PKG_VERSION));
		break;
	case PKG_EVENT_DEINSTALL_FINISHED:
		printf(" done\n");
		break;
	case PKG_EVENT_REQUIRED:
		pkg = ev->e_required.pkg;
		fprintf(stderr, "%s-%s is required by:", pkg_get(pkg, PKG_NAME),
				pkg_get(pkg, PKG_VERSION));
		while (pkg_rdeps(pkg, &dep) == EPKG_OK) {
			fprintf(stderr, " %s-%s", pkg_dep_name(dep), pkg_dep_version(dep));
		}
		if (ev->e_required.force == 1)
			fprintf(stderr, ", deleting anyway\n");
		else
			fprintf(stderr, "\n");
		break;
	default:
		break;
	}
	return 0;
}
