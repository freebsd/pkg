#include <err.h>

#include "pkg.h"
#include "event.h"

int
event_callback(void *data __unused, struct pkg_event *ev)
{
	unsigned int percent;

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
		break;
	default:
		break;
	}
	return 0;
}
