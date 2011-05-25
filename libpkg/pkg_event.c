#include <assert.h>
#include <archive.h>
#include "pkg.h"
#include "pkg_error.h"

/* Guard-rail against incorrect number of arguments */
static void
pkg_event_argument_check(pkg_event_t ev, int argc)
{

	switch(ev) {
	case PKG_EVENT_INSTALL_BEGIN:
		assert(argc == 1);
		break;
	case PKG_EVENT_ARCHIVE_ERROR:
		assert(argc == 2);
		break;
	default:
		break;
	}
}

/**
 * This function's purpose is to perform global event handling.
 */
static void
libpkg_handle_event(pkg_event_t ev, void **argv)
{
	switch(ev) {
	case PKG_EVENT_ARCHIVE_ERROR:
		pkg_error_set(EPKG_FATAL, "archive_read_disk_entry_from_file(%s): %s",
		    argv[0], archive_error_string(argv[1]));
		break;
	case PKG_EVENT_OPEN_FAILED:
		pkg_error_set(EPKG_FATAL, "open of %s failed: %s", argv[0], argv[1]);
		break;
	default:
		break;
	}
}

void
__pkg_emit_event(struct pkg_handle *hdl, pkg_event_t ev, int argc, ...)
{
	va_list ap;
	void **argv;
	int i;

	if (hdl == NULL || hdl->event_cb == NULL)
		return;

	pkg_event_argument_check(ev, argc);

	/* Generate the argument vector to pass in. */
	argv = calloc(argc, sizeof(void *));
	va_start(ap, argc);
	for (i = 0;i < argc; i++)
		argv[i] = va_arg(ap, void *);
	va_end(ap);

	libpkg_handle_event(ev, argv);

	if (hdl->event_cb != NULL)
		hdl->event_cb(ev, argv);
}
