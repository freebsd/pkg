#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	old_db_readonly \
	old_db_writable

# Populate the local database with a package, then turn it into what an
# older pkg(8) would have left behind: drop the objects introduced by
# the migrations following the schema version 37.
old_db_setup()
{
	atf_check -e empty -o empty -s exit:0 \
	    sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	atf_check -e empty -o empty -s exit:0 pkg create -M test.ucl
	atf_check -e empty -o empty -s exit:0 pkg add -q test-1.pkg

	atf_check -e empty -o empty -s exit:0 sh -c '
	    cat << EOF | pkg shell
PRAGMA user_version = 37;
DROP VIEW IF EXISTS flavors;
DROP TABLE IF EXISTS pkg_shlibs_required_ignore;
DROP TABLE IF EXISTS pkg_shlibs_provided_ignore;
DROP INDEX IF EXISTS provides_provide;
DROP INDEX IF EXISTS pkg_provides_provide_id;
DROP INDEX IF EXISTS requires_require;
DROP INDEX IF EXISTS pkg_requires_require_id;
DROP INDEX IF EXISTS pkg_requires_package_id;
DROP INDEX IF EXISTS config_files_package_id;
DROP INDEX IF EXISTS pkg_lua_script_package_id;
EOF
	'
}

old_db_readonly_body()
{
	old_db_setup

	test $(id -u) != 0 || atf_skip "root can always write to the database"
	chmod a-w local.sqlite

	# The database cannot be upgraded, the read only command still
	# has to be able to read it.
	atf_check -e empty -o match:"^test-1 " -s exit:0 pkg info

	# ... without modifying it
	atf_check -e empty -o inline:"37\n" -s exit:0 \
	    sh -c "echo 'PRAGMA user_version;' | pkg shell"
}

old_db_writable_body()
{
	old_db_setup

	# The database can be upgraded, which the read only command does
	# instead of failing on the objects the migrations would create.
	atf_check -e empty -o match:"^test-1 " -s exit:0 pkg info

	atf_check -e empty -o inline:"view|flavors\n" -s exit:0 sh -c \
	    "echo \"SELECT type, name FROM sqlite_master \
WHERE name = 'flavors';\" | pkg shell"
	atf_check -e empty -o inline:"table|pkg_shlibs_provided_ignore\ntable|pkg_shlibs_required_ignore\n" \
	    -s exit:0 sh -c "echo \"SELECT type, name FROM sqlite_master \
WHERE name LIKE 'pkg_shlibs_%_ignore' AND type = 'table' ORDER BY name;\" | pkg shell"
}
