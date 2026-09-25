/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#include <stdlib.h>

#include <sqlite3.h>

#include <atf-c.h>
#include <pkg.h>
#include <private/pkgdb.h>

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

ATF_TC_WITHOUT_HEAD(readonly_fallback);
ATF_TC_BODY(readonly_fallback, tc)
{
	struct pkgdb *db = NULL;
	sqlite3_stmt *stmt = NULL;

	setenv("INSTALL_AS_USER", "yes", 1);
	setenv("PKG_DBDIR", ".", 1);
	setenv("NO_TICK", "yes", 1);
	setenv("PKG_ENABLE_PLUGINS", "false", 1);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_open(&db, PKGDB_DEFAULT));

	/* emulate a database created by an older pkg */
	ATF_REQUIRE_EQ(SQLITE_OK, sqlite3_exec(db->sqlite,
	    "DROP TABLE pkg_local", NULL, NULL, NULL));
	pkgdb_close(db);
	db = NULL;

	/*
	 * A read-only open must expose an empty pkg_local so that the SQL
	 * queries referring to it stay valid.
	 */
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_open(&db, PKGDB_DEFAULT_READONLY));
	ATF_REQUIRE_EQ(SQLITE_OK, sqlite3_prepare_v2(db->sqlite,
	    "SELECT count(*) FROM pkg_local", -1, &stmt, NULL));
	ATF_REQUIRE_EQ(SQLITE_ROW, sqlite3_step(stmt));
	ATF_REQUIRE_EQ(0, sqlite3_column_int(stmt, 0));
	sqlite3_finalize(stmt);
	pkgdb_close(db);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, set_get_unset);
	ATF_TP_ADD_TC(tp, readonly_fallback);

	return (atf_no_error());
}
