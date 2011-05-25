#include <assert.h>
#include "pkg.h"

struct pkg_handle __pkg_handle_singleton;

struct pkg_handle *
pkg_get_handle(void)
{
	return &__pkg_handle_singleton;
}

void
pkg_handle_set_event_callback(struct pkg_handle *hdl, pkg_event_cb event_cb)
{
	hdl->event_cb = event_cb;
}

pkg_event_cb
pkg_handle_get_event_callback(struct pkg_handle *hdl)
{
	return hdl->event_cb;
}

void
__pkg_emit_event(struct pkg_handle *hdl, pkg_event_t ev, int argc, ...)
{
	va_list ap;
	void **argv;
	int i;

	if (hdl == NULL || hdl->event_cb == NULL)
		return;

	/* Guard-rail against incorrect number of arguments */
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

	/* Generate the argument vector to pass in. */
	argv = calloc(argc, sizeof(void *));
	va_start(ap, argc);
	for (i = 0;i < argc; i++)
		argv[i] = va_arg(ap, void *);
	va_end(ap);

	hdl->event_cb(ev, argv);
}
