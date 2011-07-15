#include "pkg.h"
#include "pkg_event.h"

static pkg_event_cb _cb = NULL;
static void *_data = NULL;

void
pkg_event_register(pkg_event_cb cb, void *data)
{
	_cb = cb;
	_data = data;
}

void
pkg_emit_event(struct pkg_event *ev, const char *file, uint16_t line)
{
	ev->file = file;
	ev->line = line;

	if (_cb != NULL)
		_cb(_data, ev);
}
