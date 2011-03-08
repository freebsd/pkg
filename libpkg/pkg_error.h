#ifndef _PKG_ERROR_H
#define _PKG_ERROR_H

#ifdef DEBUG
#	define pkg_error_set(code, fmt, ...) \
		_pkg_error_set(code, fmt " [at %s:%d]", ##__VA_ARGS__, __FILE__, __LINE__)
#else
#	define pkg_error_set _pkg_error_set
#endif

#define ERROR_BAD_ARG(name) \
	pkg_error_set(EPKG_FATAL, "Bad argument `%s` in %s", name, __FUNCTION__)

#define ERROR_SQLITE(db) \
	pkg_error_set(EPKG_FATAL, "%s (sqlite)", sqlite3_errmsg(db))

pkg_error_t _pkg_error_set(pkg_error_t, const char *, ...);
pkg_error_t pkg_error_seterrno(void);

#endif
