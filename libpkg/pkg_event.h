#ifndef _PKG_EVENT
#define _PKG_EVENT

#define _EV_START \
	do { \
		struct pkg_event ev

#define _EV_EMIT \
	pkg_emit_event(&ev, __FILE__, __LINE__)

#define _EV_END \
	} while(0)

#define EMIT_PKG_ERROR(fmt, ...) \
	_EV_START; \
	ev.type = PKG_EVENT_ERROR; \
	asprintf(&ev.e_pkg_error.msg, fmt, __VA_ARGS__); \
	_EV_EMIT; \
	free(ev.e_pkg_error.msg); \
	_EV_END

#define EMIT_ERRNO(f, a) \
	_EV_START; \
	ev.type = PKG_EVENT_ERRNO; \
	ev.e_errno.func = f; \
	ev.e_errno.arg = a; \
	_EV_EMIT; \
	_EV_END

#define EMIT_ALREADY_INSTALLED(p) \
	_EV_START; \
	ev.type = PKG_EVENT_ALREADY_INSTALLED; \
	ev.e_already_installed.pkg = p; \
	_EV_EMIT; \
	_EV_END

#define EMIT_FETCHING(u, t, d, e) \
	_EV_START; \
	ev.type = PKG_EVENT_FETCHING; \
	ev.e_fetching.url = u; \
	ev.e_fetching.total = t; \
	ev.e_fetching.done = d; \
	ev.e_fetching.elapsed = e; \
	_EV_EMIT; \
	_EV_END

#define EMIT_INSTALL_BEGIN(p) \
	_EV_START; \
	ev.type = PKG_EVENT_INSTALL_BEGIN; \
	ev.e_begin_install.pkg = p; \
	_EV_EMIT; \
	_EV_END

#define EMIT_MISSING_DEP(p, d) \
	_EV_START; \
	ev.type = PKG_EVENT_MISSING_DEP; \
	ev.e_missing_dep.pkg = p; \
	ev.e_missing_dep.dep = d;  \
	_EV_EMIT; \
	_EV_END

#define EMIT_FAILED_CKSUM(p) \
	_EV_START; \
	ev.type = PKG_EVENT_FAILED_CKSUM; \
	ev.e_failed_cksum.pkg = p; \
	_EV_EMIT; \
	_EV_END

void pkg_emit_event(struct pkg_event *, const char *, uint16_t);

#endif
