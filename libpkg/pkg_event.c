#include <assert.h>
#include <archive.h>
#include "pkg.h"
#include "pkg_error.h"
#include <sys/sbuf.h>

/* Guard-rail against incorrect number of arguments */
static void
pkg_event_argument_check(pkg_event_t ev, int argc)
{

	switch(ev) {
	case PKG_EVENT_ALREADY_INSTALLED:
	case PKG_EVENT_CKSUM_ERROR:
	case PKG_EVENT_CONFIG_KEY_NOTFOUND:
	case PKG_EVENT_DELETE_DEP_EXISTS:
	case PKG_EVENT_FETCH_ERROR:
	case PKG_EVENT_INSTALL_BEGIN:
	case PKG_EVENT_INVALID_DB_STATE:
	case PKG_EVENT_MALLOC_ERROR:
	case PKG_EVENT_UNKNOWN_SCRIPT:
	case PKG_EVENT_SQLITE_ERROR:
		assert(argc == 1);
		break;
	case PKG_EVENT_ARCHIVE_COMP_UNSUP:
	case PKG_EVENT_ARCHIVE_ERROR:
	case PKG_EVENT_ERROR_INSTALLING_DEP:
	case PKG_EVENT_MISSING_DEP:
	case PKG_EVENT_OPEN_DB_ERROR:
	case PKG_EVENT_PARSE_ERROR:
	case PKG_EVENT_REPO_KEY_UNAVAIL:
	case PKG_EVENT_REPO_KEY_UNUSABLE:
	case PKG_EVENT_SQLITE_CONSTRAINT:
		assert(argc == 2);
		break;
	case PKG_EVENT_CREATE_DB_ERROR:
	case PKG_EVENT_IO_ERROR:
		assert(argc == 3);
		break;
	default:
		break;
	}
}

/**
 * This function's purpose is to perform global event handling.
 */
static void
libpkg_handle_event(pkg_event_t ev, const char *filename, int line, void **argv)
{
	pkg_error_t ret = EPKG_FATAL; /* most of these are this code */
	struct sbuf *sb;

	sb = sbuf_new_auto();
	switch(ev) {
	case PKG_EVENT_ALREADY_INSTALLED:
		sbuf_printf(sb, "package '%s' already installed", pkg_get(argv[0], PKG_NAME));
		ret = EPKG_INSTALLED;
		break;
	case PKG_EVENT_ARCHIVE_COMP_UNSUP:
		ret = EPKG_OK; /* XXX needs an error message? */
		break;
	case PKG_EVENT_ARCHIVE_ERROR:
		sbuf_printf(sb, "archive error at %s: %s", (const char *)argv[0], archive_error_string(argv[1]));
		break;
	case PKG_EVENT_CKSUM_ERROR:
		sbuf_printf(sb, "package '%s' failed checksum", pkg_get(argv[0], PKG_NAME));
		break;
	case PKG_EVENT_CONFIG_KEY_NOTFOUND:
		sbuf_printf(sb, "unknown configuration key `%s'", (const char *)argv[0]);
		break;
	case PKG_EVENT_CREATE_DB_ERROR:
		if (argv[2] == NULL)
			sbuf_printf(sb, "%s: %s", (char *)argv[0], (char *)argv[1]);
		else
			sbuf_printf(sb, "%s(%s): %s", (char *)argv[0], (char *)argv[1], (char *)argv[2]);
		break;
	case PKG_EVENT_DELETE_DEP_EXISTS:
		sbuf_printf(sb, "%s", (char *)argv[0]);
		ret = EPKG_REQUIRED;
		break;
	case PKG_EVENT_ERROR_INSTALLING_DEP:
		sbuf_printf(sb, "error while installing dependency %s: %s", (char *)argv[0], (char *)argv[1]);
		break;
	case PKG_EVENT_FETCH_ERROR:
		sbuf_printf(sb, "%s", (char *)argv[0]);
		break;
	case PKG_EVENT_INVALID_DB_STATE:
		sbuf_printf(sb, "%s", (char *)argv[0]);
		break;
	case PKG_EVENT_IO_ERROR:
		sbuf_printf(sb, "I/O error: %s(%s): %s", /*call*/(char *)argv[0],
		    /*arg*/(char *)argv[1], /*strerror*/(char *)argv[2]);
		break;
	case PKG_EVENT_MALLOC_ERROR:
		sbuf_printf(sb, "allocation error: %s", (char *)argv[0]);
		break;
	case PKG_EVENT_MISSING_DEP:
		sbuf_printf(sb, "missing %s-%s dependency", (char *)argv[0], (char *)argv[1]);
		ret = EPKG_DEPENDENCY;
		break;
	case PKG_EVENT_OPEN_DB_ERROR:
		sbuf_printf(sb, "db open(%s) failed: %s", (char *)argv[0], (char *)argv[1]);
		break;
	case PKG_EVENT_PARSE_ERROR:
		sbuf_printf(sb, "parse error(%s): %s", (char *)argv[0], (char *)argv[1]);
		break;
	case PKG_EVENT_REPO_KEY_UNAVAIL:
		sbuf_printf(sb, "RSA key %s invalid: %s", (char *)argv[0], (char *)argv[1]);
		break;
	case PKG_EVENT_REPO_KEY_UNUSABLE:
		sbuf_printf(sb, "RSA key %s unusable: %s", (char *)argv[0], (char *)argv[1]);
		break;
	case PKG_EVENT_SQLITE_CONSTRAINT:
		sbuf_printf(sb, "constraint violation on %s: %s", (char *)argv[0], (char *)argv[1]);
		break;
	case PKG_EVENT_SQLITE_ERROR:
		sbuf_printf(sb, "sqlite error: %s", (char *)argv[0]);
		break;
	case PKG_EVENT_UNKNOWN_SCRIPT:
		sbuf_printf(sb, "unknown script '%s'", (char *)argv[0]);
		break;
	default:
		ret = EPKG_OK; /* unhandled error */
		break;
	}
	if (filename != NULL && line >= 0)
		sbuf_printf(sb, " [at %s:%d]", filename, line);
	sbuf_done(sb);
	if (ret != 0)
		pkg_error_set(ret, sbuf_data(sb));
	sbuf_delete(sb);
}

void
__pkg_emit_event(struct pkg_handle *hdl, const char *filename, int line, pkg_event_t ev, int argc, ...)
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

	libpkg_handle_event(ev, filename, line, argv);

	if (hdl->event_cb != NULL)
		hdl->event_cb(ev, filename, line, argv);
	free(argv);
}
