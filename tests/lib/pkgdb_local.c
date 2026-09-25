/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#include <stdlib.h>

#include <atf-c.h>
#include <pkg.h>

/*
 * Unit tests for the local (user) package attributes store.  The
 * pkg_local table is created on demand and is kept out of the versioned
 * database schema.
 */

ATF_TC_WITHOUT_HEAD(set_get_unset);
ATF_TC_BODY(set_get_unset, tc)
{
	struct pkgdb *db = NULL;
	char *value = NULL;

	setenv("INSTALL_AS_USER", "yes", 1);
	setenv("PKG_DBDIR", ".", 1);
	setenv("NO_TICK", "yes", 1);
	setenv("PKG_ENABLE_PLUGINS", "false", 1);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_open(&db, PKGDB_DEFAULT));

	/* unset by default */
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_get(db, "testpkg", "vital", &value));
	ATF_REQUIRE(value == NULL);

	/* set */
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_set(db, "testpkg", "vital", "1"));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_get(db, "testpkg", "vital", &value));
	ATF_REQUIRE(value != NULL);
	ATF_REQUIRE_STREQ("1", value);
	free(value);
	value = NULL;

	/* overwrite */
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_set(db, "testpkg", "vital", "0"));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_get(db, "testpkg", "vital", &value));
	ATF_REQUIRE(value != NULL);
	ATF_REQUIRE_STREQ("0", value);
	free(value);
	value = NULL;

	/* keys and packages are independent */
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_set(db, "testpkg", "pin", "yes"));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_set(db, "other", "vital", "1"));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_get(db, "testpkg", "vital", &value));
	ATF_REQUIRE(value != NULL);
	ATF_REQUIRE_STREQ("0", value);
	free(value);
	value = NULL;

	/* unset */
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_set(db, "testpkg", "vital", NULL));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_get(db, "testpkg", "vital", &value));
	ATF_REQUIRE(value == NULL);
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_local_get(db, "testpkg", "pin", &value));
	ATF_REQUIRE(value != NULL);
	ATF_REQUIRE_STREQ("yes", value);
	free(value);

	pkgdb_close(db);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, set_get_unset);

	return (atf_no_error());
}
