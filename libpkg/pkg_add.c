#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>

#include "pkg.h"
#include "pkg_private.h"

#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM| \
		ARCHIVE_EXTRACT_TIME  |ARCHIVE_EXTRACT_ACL | \
		ARCHIVE_EXTRACT_FFLAGS|ARCHIVE_EXTRACT_XATTR)

static int
pkg_extract(const char *path)
{
	struct archive *a;
	struct archive_entry *ae;

	int ret;

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	if (archive_read_open_filename(a, path, 4096) != ARCHIVE_OK) {
		archive_read_finish(a);
		return (-1);
	}

	while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
		if (archive_entry_pathname(ae)[0] == '+') {
			archive_read_data_skip(a);
		} else {
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
		}
	}

	/* this should never happen */
	if (ret != ARCHIVE_EOF)
		return (EPKG_ERROR_ARCHIVE);

	archive_read_finish(a);

	return (EPKG_OK);
}

int
pkg_add(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_exec **execs;
	struct pkg_script **scripts;
	struct sbuf *script_cmd;
	int i;

	if (pkg_type(pkg) != PKG_FILE || pkg->path == NULL)
		return (EPKG_BAD_PACKAGE);

	script_cmd = sbuf_new_auto();

	/* execute pre-install scripts */
	if ((scripts = pkg_scripts(pkg)) != NULL)
		for (i= 0; scripts[i] != NULL; i++) {
			switch (pkg_script_type(scripts[i])) {
				case PKG_SCRIPT_INSTALL:
					sbuf_reset(script_cmd);
					sbuf_printf(script_cmd, "set -- %s-%s INSTALL\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
					sbuf_finish(script_cmd);
					system(sbuf_data(script_cmd));
					break;
				case PKG_SCRIPT_PRE_INSTALL:
					sbuf_reset(script_cmd);
					sbuf_printf(script_cmd, "set -- %s-%s\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
					sbuf_finish(script_cmd);
					system(sbuf_data(script_cmd));
					break;
				default:
					/* just ignore */
					break;
			}
		}

	if (pkg_extract(pkg->path) != EPKG_OK)
		return (EPKG_ERROR_ARCHIVE);

	/* execute post install scripts */
	if (scripts != NULL)
		for (i= 0; scripts[i] != NULL; i++) {
			switch (pkg_script_type(scripts[i])) {
				case PKG_SCRIPT_INSTALL:
					sbuf_reset(script_cmd);
					sbuf_printf(script_cmd, "set -- %s-%s POST-INSTALL\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
					sbuf_finish(script_cmd);
					system(sbuf_data(script_cmd));
					break;
				case PKG_SCRIPT_POST_INSTALL:
					sbuf_reset(script_cmd);
					sbuf_printf(script_cmd, "set -- %s-%s\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
					sbuf_finish(script_cmd);
					system(sbuf_data(script_cmd));
					break;
				default:
					/* just ignore */
					break;
			}
		}

	sbuf_free(script_cmd);

	/* execute @exec */
	if ((execs = pkg_execs(pkg)) != NULL)
		for (i = 0; execs[i] != NULL; i++)
			if (pkg_exec_type(execs[i]) == PKG_EXEC)
				system(pkg_exec_cmd(execs[i]));


	return (pkgdb_register_pkg(db, pkg));
}
