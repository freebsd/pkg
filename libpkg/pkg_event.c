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
		pkg_error_set(EPKG_FATAL, "archive error at %s: %s",
		    argv[0], archive_error_string(argv[1]));
		break;
	case PKG_EVENT_OPEN_FAILED:
		pkg_error_set(EPKG_FATAL, "open of %s failed: %s", argv[0], argv[1]);
		break;
	case PKG_EVENT_FETCH_FAILED:
		pkg_error_set(EPKG_FATAL, "%s", argv[0]);
		break;
	case PKG_EVENT_WRITE_FAILED:
		pkg_error_set(EPKG_FATAL, "write(%s): %s", argv[0], argv[1]);
		break;
	case PKG_EVENT_MALLOC_FAILED:
		pkg_error_set(EPKG_FATAL, "malloc: %s", argv[0]);
		break;
	case PKG_EVENT_UNKNOWN_SCRIPT:
		pkg_error_set(EPKG_FATAL, "unknown script '%s'", argv[0]);
		break;
	case PKG_EVENT_ALREADY_INSTALLED:
		pkg_error_set(EPKG_INSTALLED, "package '%s' already installed",
		    pkg_get(argv[0], PKG_NAME));
		break;
	case PKG_EVENT_ERROR_INSTALLING_DEP:
		pkg_error_set(EPKG_FATAL, "error while installing dependency %s: %s",
		    argv[0], argv[1]);
		break;
	case PKG_EVENT_MISSING_DEP:
		pkg_error_set(EPKG_DEPENDENCY, "missing %s-%s dependency",
		    argv[0], argv[1]);
		break;
	case PKG_EVENT_CONFIG_KEY_NOTFOUND:
		pkg_error_set(EPKG_FATAL, "unknown configuration key `%s'", argv[0]);
		break;
	case PKG_EVENT_CREATEDB_FAILED:
		pkg_error_set(EPKG_FATAL, "%s: %s", argv[0], argv[1]);
		break;
	case PKG_EVENT_CREATEDB_FAILED_ERRNO:
		pkg_error_set(EPKG_FATAL, "%s(%s): %s", argv[0], argv[1], argv[2]);
		break;
	case PKG_EVENT_SQLITE_ERROR:
		pkg_error_set(EPKG_FATAL, "sqlite: %s", argv[0]);
		break;
	case PKG_EVENT_OPEN_DB_FAILED:
		pkg_error_set(EPKG_FATAL, "db open(%s) failed: %s", argv[0], argv[1]);
		break;
	case PKG_EVENT_REPO_KEY_UNAVAIL:
		pkg_error_set(EPKG_FATAL, "RSA key %s invalid: %s", argv[0], argv[1]);
		break;
	case PKG_EVENT_REPO_KEY_UNUSABLE:
		pkg_error_set(EPKG_FATAL, "RSA key %s unusable: %s", argv[0], argv[1]);
		break;
	case PKG_EVENT_DELETE_DEP_EXISTS:
		pkg_error_set(EPKG_REQUIRED, "%s", argv[0]);
		break;
	case PKG_EVENT_PARSE_ERROR:
		pkg_error_set(EPKG_FATAL, "parse error(%s): %s", argv[0], argv[1]);
		break;
	case PKG_EVENT_CKSUM_FAILED:
		pkg_error_set(EPKG_FATAL, "package '%s' failed checksum",
		    pkg_get(argv[0], PKG_NAME));
		break;
	case PKG_EVENT_FSTAT_ERROR:
		pkg_error_set(EPKG_FATAL, "fstat(%s): %s", argv[0], argv[1]);
		break;
	case PKG_EVENT_READ_ERROR:
		pkg_error_set(EPKG_FATAL, "read(%s): %s\n", argv[0], argv[1]);
		break;
	case PKG_EVENT_ACCESS_ERROR:
		pkg_error_set(EPKG_FATAL, "access(%s): %s\n", argv[0], argv[1]);
		break;
	case PKG_EVENT_INVALID_DB_STATE:
		pkg_error_set(EPKG_FATAL, "%s", argv[0]);
		break;
	case PKG_EVENT_SQLITE_CONSTRAINT:
		/* XXX: this is not specific enough - see original usage */
		pkg_error_set(EPKG_FATAL, "constraint violation: %s", argv[0]);
		break;
	case PKG_EVENT_ARCHIVE_COMP_UNSUP:
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

	if (hdl == NULL)
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
	free(argv);
}
