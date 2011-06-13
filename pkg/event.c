#include <archive.h>
#include <err.h>

#include "pkg.h"
#include "event.h"

int
event_callback(pkg_event_t ev, const char *filename, int line, void **argv)
{

	switch(ev) {
	case PKG_EVENT_INSTALL_BEGIN:
		printf("Installing %s\n", pkg_get((struct pkg *)argv[0], PKG_NAME));
		break;
	case PKG_EVENT_ARCHIVE_ERROR:
		fprintf(stderr, "archive error on %s: %s [at %s:%d]\n",
		    (const char *)argv[0], archive_error_string(argv[1]),
		    filename, line);
		break;
	case PKG_EVENT_ARCHIVE_COMP_UNSUP:
		warnx("%s is not supported, trying %s",
		    (const char *)argv[0], (const char *)argv[1]);
		break;
	default:
		break;
	}
	return 0;
}
