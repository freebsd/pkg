/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DB_UPGRADES
#define _DB_UPGRADES

static struct db_upgrades {
	int version;
	const char *sql;
} db_upgrades[] = {
	{1,
	"CREATE TABLE licenses ("
		"id INTEGER PRIMARY KEY, "
		"license TEXT NOT NULL UNIQUE "
	");"
	"CREATE TABLE pkg_licenses_assoc ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE, "
		"license_id INTEGER REFERENCES licenses(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT, "
		"PRIMARY KEY (package_id, license_id)"
	");"
	"CREATE VIEW pkg_licenses AS SELECT origin, license FROM packages "
	"INNER JOIN pkg_licenses_assoc ON packages.id = pkg_licenses_assoc.package_id "
	"INNER JOIN licenses ON pkg_licenses_assoc.license_id = licenses.id;"
	"CREATE TRIGGER license_insert INSTEAD OF INSERT ON pkg_licenses "
		"FOR EACH ROW BEGIN "
			"INSERT OR IGNORE INTO licenses(license) values (NEW.license);"
			"INSERT INTO pkg_licenses_assoc(package_id, license_id) VALUES "
				"((SELECT id FROM packages where origin = NEW.origin), "
				"(SELECT id FROM categories WHERE name = NEW.name));"
		"END;"
	},

	{2,
	"ALTER TABLE packages ADD licenselogic INTEGER NOT NULL DEFAULT(1);"
	},

	{3,
	"DROP VIEW pkg_licenses;"
	"DROP TRIGGER license_insert;"
	"ALTER TABLE licenses RENAME TO todelete;"
	"CREATE TABLE licenses (id INTERGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);"
	"INSERT INTO licenses(id, name) SELECT id, license FROM todelete;"
	"CREATE VIEW pkg_licenses AS SELECT origin, licenses.name FROM packages "
	"INNER JOIN pkg_licenses_assoc ON packages.id = pkg_licenses_assoc.package_id "
	"INNER JOIN licenses ON pkg_licenses_assoc.license_id = licenses.id;"
	"CREATE TRIGGER license_insert INSTEAD OF INSERT ON pkg_licenses "
		"FOR EACH ROW BEGIN "
			"INSERT OR IGNORE INTO licenses(name) values (NEW.name);"
			"INSERT INTO pkg_licenses_assoc(package_id, license_id) VALUES "
				"((SELECT id FROM packages where origin = NEW.origin), "
				"(SELECT id FROM licenses WHERE name = NEW.name));"
		"END;"
	"DROP VIEW pkg_mtree;"
	"CREATE VIEW pkg_mtree AS "
		"SELECT origin, name, version, comment, desc, mtree.content AS "
			"mtree, message, arch, osversion, maintainer, www, prefix, "
			"flatsize, automatic, licenselogic, pkg_format_version "
			"FROM packages "
	"INNER JOIN mtree ON packages.mtree_id = mtree.id;"
	"DROP TRIGGER pkg_insert;"
	"CREATE TRIGGER pkg_insert INSTEAD OF INSERT ON pkg_mtree "
		"FOR EACH ROW BEGIN "
			"INSERT OR IGNORE INTO mtree (content) VALUES (NEW.mtree);"
			"INSERT OR REPLACE INTO packages(origin, name, version, comment, desc, mtree_id, "
				"message, arch, osversion, maintainer, www, prefix, flatsize, automatic, licenselogic) "
				"VALUES (NEW.origin, NEW.name, NEW.version, NEW.comment, NEW.desc, "
				"(SELECT id FROM mtree WHERE content = NEW.mtree), "
				"NEW.message, NEW.arch, NEW.osversion, NEW.maintainer, NEW.www, NEW.prefix, "
				"NEW.flatsize, NEW.automatic, NEW.licenselogic);"
		"END;"
	"DROP TABLE todelete;"
	},
	{4,
	"DROP VIEW pkg_mtree;"
	"DROP TRIGGER CLEAN_MTREE;"
	"DROP TRIGGER pkg_insert;"
	"DROP VIEW pkg_dirs;"
	"DROP TRIGGER dir_insert;"
	"ALTER TABLE pkg_dirs_assoc RENAME TO pkg_directories;"
	"DROP VIEW pkg_categories;"
	"DROP TRIGGER category_insert;"
	"ALTER TABLE pkg_categories_assoc RENAME TO pkg_categories;"
	"DROP VIEW pkg_licenses;"
	"DROP TRIGGER licenses_insert;"
	"ALTER TABLE pkg_licenses_assoc RENAME TO pkg_licenses;"
	},
	{5,
	"CREATE TABLE users ("
		"id INTEGER PRIMARY KEY, "
		"name TEXT NOT NULL UNIQUE "
	");"
	"CREATE TABLE pkg_users ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE, "
		"user_id INTEGER REFERENCES users(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT, "
		"UNIQUE(package_id, user_id)"
	");"
	"CREATE TABLE groups ("
		"id INTEGER PRIMARY KEY, "
		"name TEXT NOT NULL UNIQUE "
	");"
	"CREATE TABLE pkg_groups ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE, "
		"group_id INTEGER REFERENCES groups(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT, "
		"UNIQUE(package_id, group_id)"
	");"
	},
	{6,
	"ALTER TABLE pkg_directories ADD try INTEGER;"
	"UPDATE pkg_directories SET try = 1;"
	},
	{7,
	"CREATE INDEX deporigini on deps(origin);"
	},
	{8,
	"DROP TABLE conflicts;"
	},
	{9,
	"CREATE TABLE shlibs ("
		"id INTEGER PRIMARY KEY,"
		"name TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_shlibs ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
		" ON UPDATE CASCADE,"
		"shlib_id INTEGER REFERENCES shlibs(id) ON DELETE RESTRICT"
		" ON UPDATE RESTRICT,"
		"PRIMARY KEY (package_id, shlib_id)"
	");"
	},
	{10,
	"ALTER TABLE packages RENAME TO oldpkgs;"
	"UPDATE oldpkgs set arch=myarch();"
	"CREATE TABLE packages ("
		"id INTEGER PRIMARY KEY,"
		"origin TEXT UNIQUE NOT NULL,"
		"name TEXT NOT NULL,"
		"version TEXT NOT NULL,"
		"comment TEXT NOT NULL,"
		"desc TEXT NOT NULL,"
		"mtree_id INTEGER REFERENCES mtree(id) ON DELETE RESTRICT"
			" ON UPDATE CASCADE,"
		"message TEXT,"
		"arch TEXT NOT NULL, "
		"maintainer TEXT NOT NULL, "
		"www TEXT,"
		"prefix TEXT NOT NULL, "
		"flatsize INTEGER NOT NULL,"
		"automatic INTEGER NOT NULL,"
		"licenselogic INTEGER NOT NULL,"
		"pkg_format_version INTEGER "
	");"
	"INSERT INTO packages (id, origin, name, version, comment, desc, "
	"mtree_id, message, arch, maintainer, www, prefix, flatsize, "
	"automatic, licenselogic, pkg_format_version) "
	"SELECT oldpkgs.id, origin, name, version, comment, desc, mtree_id, "
	"message, arch, maintainer, www, prefix, flatsize, automatic, "
	"licenselogic, pkg_format_version FROM oldpkgs;"
	"DROP TABLE oldpkgs;"
	},
	{11,
	"ALTER TABLE packages RENAME TO oldpkgs;"
	"CREATE TABLE packages ("
		"id INTEGER PRIMARY KEY,"
		"origin TEXT UNIQUE NOT NULL,"
		"name TEXT NOT NULL,"
		"version TEXT NOT NULL,"
		"comment TEXT NOT NULL,"
		"desc TEXT NOT NULL,"
		"mtree_id INTEGER REFERENCES mtree(id) ON DELETE RESTRICT"
			" ON UPDATE CASCADE,"
		"message TEXT,"
		"arch TEXT NOT NULL,"
		"maintainer TEXT NOT NULL, "
		"www TEXT,"
		"prefix TEXT NOT NULL,"
		"flatsize INTEGER NOT NULL,"
		"automatic INTEGER NOT NULL,"
		"licenselogic INTEGER NOT NULL,"
		"infos TEXT, "
		"time INTEGER,"
		"pkg_format_version INTEGER"
	");"
	"INSERT INTO packages (id, origin, name, version, comment, desc, "
		"mtree_id, message, arch, maintainer, www, prefix, flatsize, "
		"automatic, licenselogic, pkg_format_version) "
		"SELECT id, origin, name, version, comment, desc, "
		"mtree_id, message, arch, maintainer, www, prefix, flatsize, "
		"automatic, licenselogic, pkg_format_version "
		"FROM oldpkgs;"
	"DROP TABLE oldpkgs;"
	},

	/* Mark the end of the array */
	{ -1, NULL },

};

#endif
