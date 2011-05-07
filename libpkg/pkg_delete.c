#include <string.h>
#include <err.h>
#include <unistd.h>
#include <stdlib.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_util.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, int force)
{
	struct pkg **rdeps;
	struct pkg_file **files;
	struct pkg_exec **execs;
	struct pkg_script **scripts;
	char sha256[65];
	struct sbuf *script_cmd;
	int ret, i;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (db == NULL)
		return (ERROR_BAD_ARG("db"));

	/*
	 * Ensure that we have all the informations we need
	 */
	if ((ret = pkgdb_loadrdeps(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadfiles(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadscripts(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadexecs(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadmtree(db, pkg)) != EPKG_OK)
		return (ret);

	rdeps = pkg_rdeps(pkg);
	files = pkg_files(pkg);
	scripts = pkg_scripts(pkg);
	execs = pkg_execs(pkg);

	if (rdeps[0] != NULL && force == 0)
		return (pkg_error_set(EPKG_REQUIRED, "this package is required by "
							  "other packages"));

	script_cmd = sbuf_new_auto();
	/* execute PRE_DEINSTALL */
	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_DEINSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "set -- %s-%s DEINSTALL\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_PRE_DEINSTALL:
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

	for (i = 0; files[i] != NULL; i++) {
		/* check sha256 */
		if (pkg_file_sha256(files[i])[0] != '\0' &&
			(sha256_file(pkg_file_path(files[i]), sha256) == -1 ||
			strcmp(sha256, pkg_file_sha256(files[i])) != 0))
			warnx("%s fails original SHA256 checksum, not removed",
					pkg_file_path(files[i]));

		else if (unlink(pkg_file_path(files[i])) == -1) {
			if (is_dir(pkg_file_path(files[i]))) {
				rmdir(pkg_file_path(files[i]));
			} else {
				warn("unlink(%s)", pkg_file_path(files[i]));
				continue;
			}
		}
	}

	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_DEINSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "set -- %s-%s POST-DEINSTALL\n%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_POST_DEINSTALL:
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

	/* run the @unexec */
	for (i = 0; execs[i] != NULL; i++)
		if (pkg_exec_type(execs[i]) == PKG_UNEXEC)
			system(pkg_exec_cmd(execs[i]));

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}
