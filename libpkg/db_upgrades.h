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

	/* Mark the end of the array */
	{ -1, NULL },

};

#endif
