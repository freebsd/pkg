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

const char repo_db_file[] = "repo.sqlite";
const char repo_db_archive[] = "repo";
const char repo_packagesite_file[] = "packagesite.yaml";
const char repo_packagesite_archive[] = "packagesite";
const char repo_filesite_file[] = "filesite.yaml";
const char repo_filesite_archive[] = "filesite";
const char repo_digests_file[] = "digests";
const char repo_digests_archive[] = "digests";

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
	"CREATE TABLE options ("
	    "package_id INTEGER REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "option TEXT,"
	    "value TEXT,"
	    "UNIQUE (package_id, option)"
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
	"CREATE TABLE abstract ("
	    "abstract_id INTEGER PRIMARY KEY,"
	    "abstract TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_abstract ("
	    "package_id INTERGER REFERENCES packages(id)"
	    " ON DELETE CASCADE ON UPDATE RESTRICT,"
	    "key_id INTEGER NOT NULL REFERENCES abstract(abstract_id)"
	    " ON DELETE CASCADE ON UPDATE RESTRICT,"
	    "value_id INTEGER NOT NULL REFERENCES abstract(abstract_id)"
	    " ON DELETE CASCADE ON UPDATE RESTRICT,"
	    "UNIQUE (package_id, key_id, value_id)"
	");"
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

	/* Mark the end of the array */
	{ -1, -1, NULL, NULL, }

};

/* How to downgrade a newer repo to match what the current system
   expects */
static const struct repo_changes repo_downgrades[] = {
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
