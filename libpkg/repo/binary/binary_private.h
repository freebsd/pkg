/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef INIT_PRIVATE_H_
#define INIT_PRIVATE_H_

#include <sqlite3.h>

#include "pkg.h"
#include "private/pkg.h"

static const char binary_repo_initsql[] = ""
	"CREATE TABLE packages ("
	    "id INTEGER PRIMARY KEY,"
	    "origin TEXT,"
	    "name TEXT NOT NULL,"
	    "version TEXT NOT NULL,"
	    "comment TEXT NOT NULL,"
	    "desc TEXT NOT NULL,"
	    "osversion TEXT,"
	    "arch TEXT NOT NULL,"
	    "maintainer TEXT NOT NULL,"
	    "www TEXT,"
	    "prefix TEXT NOT NULL,"
	    "pkgsize INTEGER NOT NULL,"
	    "flatsize INTEGER NOT NULL,"
	    "licenselogic INTEGER NOT NULL,"
	    "cksum TEXT NOT NULL,"
	    /* relative path to the package in the repository */
	    "path TEXT NOT NULL,"
	    "pkg_format_version INTEGER,"
	    "manifestdigest TEXT NULL,"
	    "olddigest TEXT NULL,"
	    "dep_formula TEXT NULL,"
	    "vital INTEGER NOT NULL DEFAULT 0"
	");"
	"CREATE TABLE deps ("
	    "origin TEXT,"
	    "name TEXT,"
	    "version TEXT,"
	    "package_id INTEGER REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "UNIQUE(package_id, name)"
	");"
	"CREATE TABLE categories ("
	    "id INTEGER PRIMARY KEY, "
	    "name TEXT NOT NULL UNIQUE "
	");"
	"CREATE TABLE pkg_categories ("
	    "package_id INTEGER REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "category_id INTEGER REFERENCES categories(id)"
	    "  ON DELETE RESTRICT ON UPDATE RESTRICT,"
	    "UNIQUE(package_id, category_id)"
	");"
	"CREATE TABLE licenses ("
	    "id INTEGER PRIMARY KEY,"
	    "name TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_licenses ("
	    "package_id INTEGER REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "license_id INTEGER REFERENCES licenses(id)"
	    "  ON DELETE RESTRICT ON UPDATE RESTRICT,"
	    "UNIQUE(package_id, license_id)"
	");"
	"CREATE TABLE option ("
		"option_id INTEGER PRIMARY KEY,"
		"option TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE option_desc ("
		"option_desc_id INTEGER PRIMARY KEY,"
		"option_desc TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_option ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"value TEXT NOT NULL,"
		"PRIMARY KEY(package_id, option_id)"
	");"
	"CREATE TABLE pkg_option_desc ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"option_desc_id INTEGER NOT NULL "
			"REFERENCES option_desc(option_desc_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"PRIMARY KEY(package_id, option_id)"
	");"
	"CREATE TABLE pkg_option_default ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"default_value TEXT NOT NULL,"
		"PRIMARY KEY(package_id, option_id)"
	");"
	"CREATE TABLE shlibs ("
	    "id INTEGER PRIMARY KEY,"
	    "name TEXT NOT NULL UNIQUE "
	");"
	"CREATE TABLE pkg_shlibs_required ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "shlib_id INTEGER NOT NULL REFERENCES shlibs(id)"
	    "  ON DELETE RESTRICT ON UPDATE RESTRICT,"
	    "UNIQUE(package_id, shlib_id)"
	");"
	"CREATE TABLE pkg_shlibs_provided ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "shlib_id INTEGER NOT NULL REFERENCES shlibs(id)"
	    "  ON DELETE RESTRICT ON UPDATE RESTRICT,"
	    "UNIQUE(package_id, shlib_id)"
	");"
	"CREATE TABLE annotation ("
	    "annotation_id INTEGER PRIMARY KEY,"
	    "annotation TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_annotation ("
	    "package_id INTEGER REFERENCES packages(id)"
	    " ON DELETE CASCADE ON UPDATE RESTRICT,"
	    "tag_id INTEGER NOT NULL REFERENCES annotation(annotation_id)"
	    " ON DELETE CASCADE ON UPDATE RESTRICT,"
	    "value_id INTEGER NOT NULL REFERENCES annotation(annotation_id)"
	    " ON DELETE CASCADE ON UPDATE RESTRICT,"
	    "UNIQUE (package_id, tag_id)"
	");"
	"CREATE TABLE pkg_conflicts ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "conflict_id INTEGER NOT NULL,"
	    "UNIQUE(package_id, conflict_id)"
	");"
	"CREATE TABLE provides("
	"    id INTEGER PRIMARY KEY,"
	"    provide TEXT NOT NULL"
	");"
	"CREATE TABLE pkg_provides ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "provide_id INTEGER NOT NULL REFERENCES provides(id)"
	    "  ON DELETE RESTRICT ON UPDATE RESTRICT,"
	    "UNIQUE(package_id, provide_id)"
	");"
	"CREATE TABLE requires("
	"    id INTEGER PRIMARY KEY,"
	"    require TEXT NOT NULL"
	");"
	"CREATE TABLE pkg_requires ("
		"package_id INTEGER NOT NULL REFERENCES packages(id)"
		"  ON DELETE CASCADE ON UPDATE CASCADE,"
		"require_id INTEGER NOT NULL REFERENCES requires(id)"
		"  ON DELETE RESTRICT ON UPDATE RESTRICT,"
		"UNIQUE(package_id, require_id)"
	");"
/*	"CREATE INDEX packages_origin ON packages(origin COLLATE NOCASE);"
	"CREATE INDEX packages_name ON packages(name COLLATE NOCASE);"
	"CREATE INDEX packages_uid_nocase ON packages(name COLLATE NOCASE, origin COLLATE NOCASE);"
	"CREATE INDEX packages_version_nocase ON packages(name COLLATE NOCASE, version);"
	"CREATE INDEX packages_uid ON packages(name, origin);"
	"CREATE INDEX packages_version ON packages(name, version);"
	"CREATE UNIQUE INDEX packages_digest ON packages(manifestdigest);"*/

	"PRAGMA user_version=%d;"
	;

/* The package repo schema major revision */
#define REPO_SCHEMA_MAJOR 2

/* The package repo schema minor revision.
   Minor schema changes don't prevent older pkgng
   versions accessing the repo. */
#define REPO_SCHEMA_MINOR 14

#define REPO_SCHEMA_VERSION (REPO_SCHEMA_MAJOR * 1000 + REPO_SCHEMA_MINOR)

#define REPO_NAME_PREFIX "repo-"

typedef enum _sql_prstmt_index {
	PKG = 0,
	DEPS,
	CAT1,
	CAT2,
	LIC1,
	LIC2,
	OPT1,
	OPT2,
	SHLIB1,
	SHLIB_REQD,
	SHLIB_PROV,
	ANNOTATE1,
	ANNOTATE2,
	EXISTS,
	REPO_VERSION,
	DELETE,
	PROVIDE,
	PROVIDES,
	REQUIRE,
	REQUIRES,
	PRSTMT_LAST,
} sql_prstmt_index;

int pkg_repo_binary_init_prstatements(sqlite3 *sqlite);
int pkg_repo_binary_run_prstatement(sql_prstmt_index s, ...);
const char * pkg_repo_binary_sql_prstatement(sql_prstmt_index s);
sqlite3_stmt* pkg_repo_binary_stmt_prstatement(sql_prstmt_index s);
void pkg_repo_binary_finalize_prstatements(void);
/*
 * Warning: returns a pointer to static array
 */
const char * pkg_repo_binary_get_filename(struct pkg_repo *);

#endif /* INIT_PRIVATE_H_ */
