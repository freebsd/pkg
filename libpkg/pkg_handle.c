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
