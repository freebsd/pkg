/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
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

#ifndef _REPODB
#define _REPODB

static const char repo_db_file[] = "repo.sqlite";
static const char repo_db_archive[] = "repo";
static const char repo_packagesite_file[] = "packagesite.yaml";
static const char repo_packagesite_archive[] = "packagesite";
static const char repo_filesite_file[] = "filesite.yaml";
static const char repo_filesite_archive[] = "filesite";
static const char repo_digests_file[] = "digests";
static const char repo_digests_archive[] = "digests";
static const char repo_conflicts_file[] = "conflicts";
static const char repo_conflicts_archive[] = "conflicts";

static const char initsql[] = ""
	"CREATE TABLE packages ("
	    "id INTEGER PRIMARY KEY,"
	    "origin TEXT UNIQUE,"
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
	    "manifestdigest TEXT NULL"
	");"
	"CREATE TABLE deps ("
	    "origin TEXT,"
	    "name TEXT,"
	    "version TEXT,"
	    "package_id INTEGER REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "UNIQUE(package_id, origin)"
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
	    "package_id INTERGER REFERENCES packages(id)"
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
	"CREATE INDEX packages_origin ON packages(origin COLLATE NOCASE);"
	"CREATE INDEX packages_name ON packages(name COLLATE NOCASE);"
	"CREATE INDEX packages_uid_nocase ON packages(name COLLATE NOCASE, origin COLLATE NOCASE);"
	"CREATE INDEX packages_version_nocase ON packages(name COLLATE NOCASE, version);"
	"CREATE INDEX packages_uid ON packages(name, origin COLLATE NOCASE);"
	"CREATE INDEX packages_version ON packages(name, version);"
	/* FTS search table */
	"CREATE VIRTUAL TABLE pkg_search USING fts4(id, name, origin);"

	"PRAGMA user_version=%d;"
	;

struct repo_changes {
	int version;		/* The repo schema this change applies to */
	int next_version;	/* The repo schema this change creates */
	const char *message;
	const char *sql;
};

/* How to upgrade an older repo to match what the current system
   expects */
static const struct repo_changes repo_upgrades[] = {
	{2001,
	 2002,
	 "Modify shlib tracking to add \'provided\' capability",

	 "CREATE TABLE %Q.pkg_shlibs_required ("
		"package_id INTEGER NOT NULL REFERENCES packages(id)"
		" ON DELETE CASCADE ON UPDATE CASCADE,"
		"shlib_id INTEGER NOT NULL REFERENCES shlibs(id)"
		" ON DELETE RESTRICT ON UPDATE RESTRICT,"
		"UNIQUE (package_id, shlib_id)"
	 ");"
	 "CREATE TABLE %Q.pkg_shlibs_provided ("
		"package_id INTEGER NOT NULL REFERENCES packages(id)"
		" ON DELETE CASCADE ON UPDATE CASCADE,"
		"shlib_id INTEGER NOT NULL REFERENCES shlibs(id)"
		" ON DELETE RESTRICT ON UPDATE RESTRICT,"
		"UNIQUE (package_id, shlib_id)"
	 ");"
	 "INSERT INTO %Q.pkg_shlibs_required (package_id, shlib_id)"
		" SELECT package_id, shlib_id FROM %Q.pkg_shlibs;"
	 "DROP TABLE %Q.pkg_shlibs;"
	},
	{2002,
	 2003,
	 "Add abstract metadata capability",

	 "CREATE TABLE %Q.abstract ("
		"abstract_id INTEGER PRIMARY KEY,"
		"abstract TEXT NOT NULL UNIQUE"
	 ");"
	 "CREATE TABLE %Q.pkg_abstract ("
		"package_id INTERGER REFERENCES packages(id)"
		" ON DELETE CASCADE ON UPDATE RESTRICT,"
		"key_id INTEGER NOT NULL REFERENCES abstract(abstract_id)"
		" ON DELETE CASCADE ON UPDATE RESTRICT,"
		"value_id INTEGER NOT NULL REFERENCES abstract(abstract_id)"
		" ON DELETE CASCADE ON UPDATE RESTRICT"
	 ");"
	},
	{2003,
	 2004,
	"Add manifest digest field",

	"ALTER TABLE %Q.packages ADD COLUMN manifestdigest TEXT NULL;"
	"CREATE INDEX IF NOT EXISTS %Q.pkg_digest_id ON packages(origin, manifestdigest);"
	},
	{2004,
	 2005,
	 "Rename 'abstract metadata' to 'annotations'",

	 "CREATE TABLE %Q.annotation ("
	        "annotation_id INTEGER PRIMARY KEY,"
	        "annotation TEXT NOT NULL UNIQUE"
	 ");"
	 "CREATE TABLE %Q.pkg_annotation ("
	        "package_id INTEGER REFERENCES packages(id)"
	        " ON DELETE CASCADE ON UPDATE RESTRICT,"
	        "tag_id INTEGER NOT NULL REFERENCES annotation(annotation_id)"
	        " ON DELETE CASCADE ON UPDATE RESTRICT,"
	        "value_id INTEGER NOT NULL REFERENCES annotation(annotation_id)"
	        " ON DELETE CASCADE ON UPDATE RESTRICT,"
	        "UNIQUE (package_id, tag_id)"
	 ");"
	 "INSERT INTO %Q.annotation (annotation_id, annotation)"
	        " SELECT abstract_id, abstract FROM %Q.abstract;"
	 "INSERT INTO %Q.pkg_annotation (package_id,tag_id,value_id)"
	        " SELECT package_id,key_id,value_id FROM %Q.pkg_abstract;"
	 "DROP TABLE pkg_abstract;"
	 "DROP TABLE abstract;"
	},
	{2005,
	 2006,
	 "Add capability to track option descriptions and defaults",

	 "CREATE TABLE %Q.option ("
		"option_id INTEGER PRIMARY KEY,"
		"option TEXT NOT NULL UNIQUE"
	 ");"
	 "CREATE TABLE %Q.option_desc ("
		"option_desc_id INTEGER PRIMARY KEY,"
		"option_desc TEXT NOT NULL UNIQUE"
	 ");"
	 "CREATE TABLE %Q.pkg_option ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"value TEXT NOT NULL,"
		"PRIMARY KEY(package_id, option_id)"
	 ");"
	 "CREATE TABLE %Q.pkg_option_desc ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"option_desc_id INTEGER NOT NULL "
			"REFERENCES option_desc(option_desc_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"PRIMARY KEY(package_id, option_id)"
	 ");"
	 "CREATE TABLE %Q.pkg_option_default ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"default_value TEXT NOT NULL,"
		"PRIMARY KEY(package_id, option_id)"
	 ");"
	 "INSERT INTO %Q.option (option) "
		"SELECT DISTINCT option FROM %Q.options;"
	 "INSERT INTO %Q.pkg_option(package_id, option_id, value) "
		"SELECT package_id, option_id, value "
		"FROM %Q.options oo JOIN %Q.option o "
			"ON (oo.option = o.option);"
	 "DROP TABLE %Q.options;",
	},
	{2006,
	 2007,
	 "Add conflicts and provides",

	"CREATE TABLE %Q.pkg_conflicts ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "conflict_id INTEGER NOT NULL,"
	    "UNIQUE(package_id, conflict_id)"
	");"
	"CREATE TABLE %Q.provides("
	"    id INTEGER PRIMARY KEY,"
	"    provide TEXT NOT NULL"
	");"
	"CREATE TABLE %Q.pkg_provides ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "provide_id INTEGER NOT NULL REFERENCES provides(id)"
	    "  ON DELETE RESTRICT ON UPDATE RESTRICT,"
	    "UNIQUE(package_id, provide_id)"
	");"
	},
	{2007,
	 2008,
	 "Add FTS index",

	 "CREATE VIRTUAL TABLE %Q.pkg_search USING fts4(id, name, origin);"
	 "INSERT INTO %Q.pkg_search SELECT id, name || '-' || version, origin FROM %Q.packages;"
	 "CREATE INDEX %Q.packages_origin ON packages(origin COLLATE NOCASE);"
	 "CREATE INDEX %Q.packages_name ON packages(name COLLATE NOCASE);"
	},
	{2008,
	 2009,
	 "Optimize indicies",

	 "CREATE INDEX IF NOT EXISTS %Q.packages_uid_nocase ON packages(name COLLATE NOCASE, origin COLLATE NOCASE);"
	 "CREATE INDEX IF NOT EXISTS %Q.packages_version_nocase ON packages(name COLLATE NOCASE, version);"
	 "CREATE INDEX IF NOT EXISTS %Q.packages_uid ON packages(name, origin COLLATE NOCASE);"
	 "CREATE INDEX IF NOT EXISTS %Q.packages_version ON packages(name, version);"
	},
	/* Mark the end of the array */
	{ -1, -1, NULL, NULL, }

};

/* How to downgrade a newer repo to match what the current system
   expects */
static const struct repo_changes repo_downgrades[] = {
	{2009,
	 2008,
	 "Drop indicies",

	 "DROP INDEX %Q.packages_uid_nocase;"
	 "DROP INDEX %Q.packages_version_nocase;"
	 "DROP INDEX %Q.packages_uid;"
	 "DROP INDEX %Q.packages_version;"
	},
	{2008,
	 2007,
	 "Drop FTS index",

	 "DROP TABLE %Q.pkg_search;"
	},
	{2007,
	 2006,
	 "Revert conflicts and provides creation",

	 "DROP TABLE %Q.pkg_provides;"
	 "DROP TABLE %Q.provides;"
	 "DROP TABLE %Q.conflicts;"
	},
	{2006,
	 2005,
	 "Revert addition of extra options related data",

	 "CREATE TABLE %Q.options ("
		"package_id INTEGER REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option TEXT,"
		"value TEXT,"
		"PRIMARY KEY(package_id,option)"
	 ");"
	 "INSERT INTO %Q.options (package_id, option, value) "
		 "SELECT package_id, option, value "
		"FROM %Q.pkg_option JOIN %Q.option USING(option_id);"
	 "DROP TABLE pkg_option;"
	 "DROP TABLE pkg_option_default;"
	 "DROP TABLE option;"
	 "DROP TABLE pkg_option_desc;"
	 "DROP TABLE option_desc;",
	},
	{2005,
	 2004,
	 "Revert rename of 'abstract metadata' to 'annotations'",

	 "CREATE TABLE %Q.abstract ("
	        "abstract_id INTEGER PRIMARY KEY,"
	        "abstract TEXT NOT NULL UNIQUE"
	 ");"
	 "CREATE TABLE %Q.pkg_abstract ("
	        "package_id INTEGER REFERENCES packages(id)"
	        " ON DELETE CASCADE ON UPDATE RESTRICT,"
	        "key_id INTEGER NOT NULL REFERENCES abstract(abstract_id)"
	        " ON DELETE CASCADE ON UPDATE RESTRICT,"
	        "value_id INTEGER NOT NULL REFERENCES abstract(abstract_id)"
	        " ON DELETE CASCADE ON UPDATE RESTRICT"
	 ");"
	 "INSERT INTO %Q.abstract (abstract_id, abstract)"
	        " SELECT annotation_id, annotation FROM %Q.annotation;"
	 "INSERT INTO %Q.pkg_abstract (package_id,key_id,value_id)"
	        " SELECT package_id,tag_id,value_id FROM %Q.pkg_annotation;"
	 "DROP TABLE %Q.pkg_annotation;"
	 "DROP TABLE %Q.annotation;"
	},
	{2004,
	 2003,
	 "Drop manifest digest index",

	 "DROP INDEX %Q.pkg_digest_id;"
	},
	{2003,
	 2002,
	 "Drop abstract metadata",

	 "DROP TABLE %Q.pkg_abstract;"
	 "DROP TABLE %Q.abstract;"
	},
	{2002,
	 2001,
	 "Drop \'shlibs provided\' but retain \'shlibs required\'",

	 "CREATE TABLE %Q.pkg_shlibs_required ("
		"package_id INTEGER NOT NULL REFERENCES packages(id)"
		" ON DELETE CASCADE ON UPDATE CASCADE,"
		"shlib_id INTEGER NOT NULL REFERENCES shlibs(id)"
		" ON DELETE RESTRICT ON UPDATE RESTRICT,"
		"UNIQUE (package_id, shlib_id)"
	 ");"
	 "CREATE TABLE %Q.pkg_shlibs ("
		"package_id INTEGER REFERENCES packages(id)"
		" ON DELETE CASCADE ON UPDATE CASCADE,"
		"shlib_id INTEGER REFERENCES shlibs(id)"
		" ON DELETE RESTRICT ON UPDATE RESTRICT,"
		"PRIMARY KEY (package_id, shlib_id)"
	 ");"
	 "INSERT INTO %Q.pkg_shlibs (package_id, shlib_id)"
		" SELECT package_id, shlib_id FROM %Q.pkg_shlibs_required;"
	 "DELETE FROM %Q.shlibs WHERE id NOT IN"
		" (SELECT shlib_id FROM %Q.pkg_shlibs);"
	 "DROP TABLE %Q.pkg_shlibs_provided;"
	 "DROP TABLE %Q.pkg_shlibs_required;"
	},


	/* Mark the end of the array */
	{ -1, -1, NULL, NULL, }

};

#endif	/* _REPODB */
