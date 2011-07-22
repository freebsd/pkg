#include <string.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkg_event.h"

int
pkgdb_dump(struct pkgdb *db, char *dest)
{
	struct pkgdb_it *it;
	struct pkg *pkg;
	struct sbuf *path;
	struct packing *pack;
	char *m;
	int ret;
	int query_flags = PKG_LOAD_DEPS | PKG_LOAD_CONFLICTS | PKG_LOAD_FILES | PKG_LOAD_CATEGORIES |
					  PKG_LOAD_DIRS | PKG_LOAD_SCRIPTS | PKG_LOAD_OPTIONS |
					  PKG_LOAD_MTREE;

	packing_init(&pack, dest ? dest : "./pkgdump", TXZ);

	path = sbuf_new_auto();
	if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL) {
		/* TODO handle errors */
		return (EPKG_FATAL);
	}

	while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
		pkg_emit_manifest(pkg, &m);
		sbuf_clear(path);
		sbuf_printf(path, "%s-%s.yaml", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		packing_append_buffer(pack, m, sbuf_data(path), strlen(m));
		free(m);
	}

	sbuf_delete(path);
	packing_finish(pack);
	return (EPKG_OK);
}
