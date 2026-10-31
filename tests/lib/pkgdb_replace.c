/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#include <sys/types.h>

#include <stdlib.h>

#include <sqlite3.h>

#include <atf-c.h>
#include <pkg.h>
#include <private/pkgdb.h>

/*
 * A batch rename has to rewrite the dependency rows too, not only the
 * packages table, or the package ends up depending on names which no
 * longer exist.
 */

static void
setup(void)
{
	setenv("INSTALL_AS_USER", "yes", 1);
	setenv("PKG_DBDIR", ".", 1);
	setenv("NO_TICK", "yes", 1);
	setenv("PKG_ENABLE_PLUGINS", "false", 1);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));
}

static void
check_dep_name(struct pkgdb *db, const char *origin, const char *expected)
{
	sqlite3_stmt *stmt = NULL;
	const char *name;

	ATF_REQUIRE_EQ(SQLITE_OK, sqlite3_prepare_v2(db->sqlite,
	    "SELECT name FROM deps WHERE origin = ?1", -1, &stmt, NULL));
	ATF_REQUIRE_EQ(SQLITE_OK,
	    sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_STATIC));
	ATF_REQUIRE_EQ(SQLITE_ROW, sqlite3_step(stmt));
	name = (const char *)sqlite3_column_text(stmt, 0);
	ATF_REQUIRE(name != NULL);
	ATF_REQUIRE_STREQ(expected, name);
	sqlite3_finalize(stmt);
}

ATF_TC_WITHOUT_HEAD(depname);
ATF_TC_BODY(depname, tc)
{
	struct pkgdb *db = NULL;

	setup();
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_open(&db, PKGDB_DEFAULT));

	ATF_REQUIRE_EQ(SQLITE_OK, sqlite3_exec(db->sqlite,
	    "INSERT INTO deps(origin, name, version) VALUES"
	    "('devel/old-dep', 'old-dep', '1.0'),"
	    "('devel/other', 'other', '2.0')", NULL, NULL, NULL));

	/* only the matching row is rewritten */
	ATF_REQUIRE_EQ(1, pkgdb_replace(db, PKG_SET_DEPNAME, "old", "new"));

	check_dep_name(db, "devel/old-dep", "new-dep");
	check_dep_name(db, "devel/other", "other");

	pkgdb_close(db);
}

ATF_TC_WITHOUT_HEAD(deporigin);
ATF_TC_BODY(deporigin, tc)
{
	struct pkgdb *db = NULL;
	sqlite3_stmt *stmt = NULL;
	const char *origin;

	setup();
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_open(&db, PKGDB_DEFAULT));

	ATF_REQUIRE_EQ(SQLITE_OK, sqlite3_exec(db->sqlite,
	    "INSERT INTO deps(origin, name, version) VALUES"
	    "('devel/old-dep', 'd', '1.0')", NULL, NULL, NULL));

	ATF_REQUIRE_EQ(1, pkgdb_replace(db, PKG_SET_DEPORIGIN, "old", "new"));

	ATF_REQUIRE_EQ(SQLITE_OK, sqlite3_prepare_v2(db->sqlite,
	    "SELECT origin FROM deps WHERE name = 'd'", -1, &stmt, NULL));
	ATF_REQUIRE_EQ(SQLITE_ROW, sqlite3_step(stmt));
	origin = (const char *)sqlite3_column_text(stmt, 0);
	ATF_REQUIRE(origin != NULL);
	ATF_REQUIRE_STREQ("devel/new-dep", origin);
	sqlite3_finalize(stmt);

	pkgdb_close(db);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, depname);
	ATF_TP_ADD_TC(tp, deporigin);

	return (atf_no_error());
}
