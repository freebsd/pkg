#include <archive.h>

#include "pkg.h"
#include "event.h"

/* XXX: use varargs? */
int
event_callback(pkg_event_t ev, void *arg0, void *arg1)
{
	struct pkg *pkg;
	const char *str0, *str1;

	switch(ev) {
	case PKG_EVENT_INSTALL_BEGIN:
		pkg = (struct pkg *)arg0;
		printf("Installing %s\n", pkg_get(pkg, PKG_NAME));
		break;
	case PKG_EVENT_ARCHIVE_ERROR:
		str0 = (const char *)arg0; /* file path */
		str1 = archive_error_string(arg1);
		fprintf(stderr, "archive error on %s: %s\n", str0, str1);
		break;
	default:
		break;
	}
	return 0;
}
