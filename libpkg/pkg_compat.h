#ifndef _PKG_COMPAT_H
#define _PKG_COMPAT_H

#include <stdbool.h>
#include <stdio.h>
#include <cJSON.h>

enum plist_t {
	PLIST_FILE, PLIST_CWD, PLIST_CMD, PLIST_CHMOD,
	PLIST_CHOWN, PLIST_CHGRP, PLIST_COMMENT, PLIST_IGNORE,
	PLIST_NAME, PLIST_UNEXEC, PLIST_SRC, PLIST_DISPLAY,
	PLIST_PKGDEP, PLIST_CONFLICTS, PLIST_MTREE, PLIST_DIR_RM,
	PLIST_IGNORE_INST, PLIST_OPTION, PLIST_ORIGIN, PLIST_DEPORIGIN,
	PLIST_NOINST
};


struct plist {
	struct plist *prev, *next;
	char *name;
	bool marked;
	enum plist_t type;
};

struct oldpackage {
	struct plist *head, *tail;
	const char *name;
	const char *origin;


};

cJSON *pkg_compat_converter(char *);

#endif
