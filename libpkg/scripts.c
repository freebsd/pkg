#include <pkg.h>
#include <pkg_private.h>
#include <pkg_error.h>

int
pkg_script_pre_install(struct pkg *pkg)
{
	int i;
	struct pkg_script **scripts;
	struct sbuf *script_cmd = sbuf_new_auto();

	if ((scripts = pkg_scripts(pkg)) == NULL)
		return (EPKG_OK);

	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_INSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s PRE-INSTALL\n%s",
					pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
					pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_PRE_INSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			default:
				/* ignored to prevent warning */
				break;
		}
	}

	sbuf_delete(script_cmd);

	return (EPKG_OK);
}

int
pkg_script_post_install(struct pkg *pkg)
{
	int i;
	struct pkg_script **scripts;
	struct sbuf *script_cmd = sbuf_new_auto();

	if ((scripts = pkg_scripts(pkg)) == NULL)
		return (EPKG_OK);

	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_INSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s POST-INSTALL\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_POST_INSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			default:
				/* ignored to prevent warning */
				break;
		}
	}

	sbuf_delete(script_cmd);

	return (EPKG_OK);
}

int
pkg_script_pre_upgrade(struct pkg *pkg)
{
	int i;
	struct pkg_script **scripts;
	struct sbuf *script_cmd = sbuf_new_auto();

	if ((scripts = pkg_scripts(pkg)) == NULL)
		return (EPKG_OK);

	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_UPGRADE:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s PRE-UPGRADE\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_PRE_UPGRADE:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			default:
				/* ignored to prevent warning */
				break;
		}
	}

	sbuf_delete(script_cmd);

	return (EPKG_OK);
}

int
pkg_script_post_upgrade(struct pkg *pkg)
{
	int i;
	struct pkg_script **scripts;
	struct sbuf *script_cmd = sbuf_new_auto();

	if ((scripts = pkg_scripts(pkg)) == NULL)
		return (EPKG_OK);

	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_UPGRADE:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s POST-UPGRADE\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_POST_UPGRADE:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			default:
				/* ignored to prevent warning */
				break;
		}
	}

	sbuf_delete(script_cmd);

	return (EPKG_OK);
}

int
pkg_script_pre_deinstall(struct pkg *pkg)
{
	int i;
	struct pkg_script **scripts;
	struct sbuf *script_cmd = sbuf_new_auto();

	if ((scripts = pkg_scripts(pkg)) == NULL)
		return (EPKG_OK);

	for (i = 0; scripts[i] != NULL; i++) {
		switch (pkg_script_type(scripts[i])) {
			case PKG_SCRIPT_DEINSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s DEINSTALL\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			case PKG_SCRIPT_PRE_DEINSTALL:
				sbuf_reset(script_cmd);
				sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s\n%s",
				  pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
				  pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
				sbuf_finish(script_cmd);
				system(sbuf_data(script_cmd));
				break;
			default:
				/* ignored to prevent warning */
				break;
		}
	}

	sbuf_delete(script_cmd);

	return (EPKG_OK);
}

int
pkg_script_post_deinstall(struct pkg *pkg)
{
	int i;
	struct pkg_script **scripts;
	struct sbuf *script_cmd = sbuf_new_auto();

	if ((scripts = pkg_scripts(pkg)) == NULL)
		return (EPKG_OK);

	/* two loops because the order matters */
	for (i = 0; scripts[i] != NULL; i++) {
		if (pkg_script_type(scripts[i]) == PKG_SCRIPT_DEINSTALL) {
			sbuf_reset(script_cmd);
			sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s POST-DEINSTALL\n%s",
					pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
					pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
			sbuf_finish(script_cmd);
			system(sbuf_data(script_cmd));
		}
	}

	for (i = 0; scripts[i] != NULL; i++) {
		if (pkg_script_type(scripts[i]) == PKG_SCRIPT_POST_DEINSTALL) {
			sbuf_reset(script_cmd);
			sbuf_printf(script_cmd, "PKG_PREFIX=%s\nset -- %s-%s\n%s",
					pkg_get(pkg, PKG_PREFIX), pkg_get(pkg, PKG_NAME),
					pkg_get(pkg, PKG_VERSION), pkg_script_data(scripts[i]));
			sbuf_finish(script_cmd);
			system(sbuf_data(script_cmd));
		}
	}

	sbuf_delete(script_cmd);

	return (EPKG_OK);
}

