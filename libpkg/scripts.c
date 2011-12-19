#include <assert.h>

#include "pkg.h"
#include "pkg_private.h"

int
pkg_script_run(struct pkg * const pkg, pkg_script_t type)
{
	struct pkg_script *script = NULL;
	pkg_script_t stype;
	struct sbuf * const script_cmd = sbuf_new_auto();
	size_t i;
	const char *name, *prefix, *version;

	struct {
		const char * const arg;
		const pkg_script_t b;
		const pkg_script_t a;
	} const map[] = {
		/* a implies b with argument arg */
		{"PRE-INSTALL",    PKG_SCRIPT_INSTALL,   PKG_SCRIPT_PRE_INSTALL},
		{"POST-INSTALL",   PKG_SCRIPT_INSTALL,   PKG_SCRIPT_POST_INSTALL},
		{"PRE-UPGRADE",    PKG_SCRIPT_UPGRADE,   PKG_SCRIPT_PRE_UPGRADE},
		{"POST-UPGRADE",   PKG_SCRIPT_UPGRADE,   PKG_SCRIPT_POST_UPGRADE},
		{"DEINSTALL",      PKG_SCRIPT_DEINSTALL, PKG_SCRIPT_PRE_DEINSTALL},
		{"POST-DEINSTALL", PKG_SCRIPT_DEINSTALL, PKG_SCRIPT_POST_DEINSTALL},
	};

	pkg_get(pkg, PKG_PREFIX, &prefix, PKG_NAME, &name, PKG_VERSION, &version);

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (map[i].a == type)
			break;
	}

	assert(map[i].a == type);

	while (pkg_scripts(pkg, &script) == EPKG_OK) {

		stype = pkg_script_type(script);

		if (stype == map[i].a || stype == map[i].b) {
			sbuf_reset(script_cmd);
			sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s",
			    prefix, name, version);

			if (stype == map[i].b) {
				/* add arg **/
				sbuf_cat(script_cmd, " ");
				sbuf_cat(script_cmd, map[i].arg);
			}

			sbuf_cat(script_cmd, "\n");
			sbuf_cat(script_cmd, pkg_script_data(script));
			sbuf_finish(script_cmd);
			system(sbuf_data(script_cmd));
		}
	}

	sbuf_delete(script_cmd);

	return (EPKG_OK);
}

