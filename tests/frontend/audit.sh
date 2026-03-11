#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	audit_vulnerable \
	audit_not_vulnerable \
	audit_empty_db \
	audit_out_of_range \
	audit_quiet \
	audit_recursive \
	audit_raw_json \
	audit_raw_ucl \
	audit_pattern \
	audit_multiple_vulns \
	audit_multiple_packages \
	audit_glob_name \
	audit_no_db

# Helper: install test packages
setup_packages() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1.5" "/usr/local"
	atf_check -o ignore pkg register -M test.ucl
}

# Helper: create a vuln XML with a vulnerability affecting test >=1.0 <2.0
create_vuln_db() {
	cat > vuln.xml << 'EOF'
<?xml version="1.0" encoding="utf-8"?>
<vuxml xmlns="http://www.vuxml.org/apps/vuxml-1">
  <vuln vid="test-vuln-001">
    <topic>Test vulnerability in test package</topic>
    <affects>
      <package>
        <name>test</name>
        <range>
          <ge>1.0</ge>
          <lt>2.0</lt>
        </range>
      </package>
    </affects>
    <references>
      <cvename>CVE-2024-00001</cvename>
    </references>
  </vuln>
</vuxml>
EOF
}

audit_vulnerable_body() {
	setup_packages
	create_vuln_db

	# test-1.5 is in range [1.0, 2.0) -> vulnerable
	atf_check \
		-o match:"test-1.5 is vulnerable" \
		-o match:"Test vulnerability in test package" \
		-o match:"CVE-2024-00001" \
		-o match:"1 problem" \
		-s exit:1 \
		pkg audit -f vuln.xml
}

audit_not_vulnerable_body() {
	# Install a package outside the vulnerable range
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "3.0" "/usr/local"
	atf_check -o ignore pkg register -M test.ucl
	create_vuln_db

	# test-3.0 is NOT in range [1.0, 2.0) -> safe
	atf_check \
		-o match:"0 problem" \
		-s exit:0 \
		pkg audit -f vuln.xml
}

audit_empty_db_body() {
	setup_packages

	cat > vuln.xml << 'EOF'
<?xml version="1.0" encoding="utf-8"?>
<vuxml xmlns="http://www.vuxml.org/apps/vuxml-1">
</vuxml>
EOF

	# Empty vuln db -> no problems
	atf_check \
		-o match:"0 problem" \
		-s exit:0 \
		pkg audit -f vuln.xml
}

audit_out_of_range_body() {
	setup_packages

	# Vulnerability only affects versions < 1.0
	cat > vuln.xml << 'EOF'
<?xml version="1.0" encoding="utf-8"?>
<vuxml xmlns="http://www.vuxml.org/apps/vuxml-1">
  <vuln vid="old-vuln">
    <topic>Old vulnerability</topic>
    <affects>
      <package>
        <name>test</name>
        <range>
          <lt>1.0</lt>
        </range>
      </package>
    </affects>
    <references>
      <cvename>CVE-2020-99999</cvename>
    </references>
  </vuln>
</vuxml>
EOF

	# test-1.5 >= 1.0, so not affected
	atf_check \
		-o match:"0 problem" \
		-s exit:0 \
		pkg audit -f vuln.xml
}

audit_quiet_body() {
	setup_packages
	create_vuln_db

	# -q: quiet mode shows only package name-version
	atf_check \
		-o inline:"test-1.5\n" \
		-s exit:1 \
		pkg audit -qf vuln.xml
}

audit_recursive_body() {
	setup_packages
	create_vuln_db

	# Create a package that depends on the vulnerable one
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "rdep" "rdep" "1.0" "/usr/local"
	cat << EOF >> rdep.ucl
deps: {
    test: {
        origin: test,
        version: "1.5"
    }
}
EOF
	atf_check -o ignore pkg register -M rdep.ucl

	# -r: show reverse dependencies of vulnerable packages
	atf_check \
		-o match:"test-1.5 is vulnerable" \
		-o match:"rdep" \
		-s exit:1 \
		pkg audit -rf vuln.xml
}

audit_raw_json_body() {
	atf_require python3 "Requires python3 to run this test"
	setup_packages
	create_vuln_db

	# -Rjson: raw JSON output
	atf_check \
		-o save:out.json \
		-s exit:1 \
		pkg audit -f vuln.xml -Rjson

	# Must be valid JSON
	atf_check -o ignore -e empty python3 -m json.tool out.json

	# Check content
	atf_check \
		-o inline:"1\n" \
		python3 -c "import json; d=json.load(open('out.json')); print(d['pkg_count'])"

	atf_check \
		-o inline:"1.5\n" \
		python3 -c "import json; d=json.load(open('out.json')); print(d['packages']['test']['version'])"

	atf_check \
		-o inline:"CVE-2024-00001\n" \
		python3 -c "import json; d=json.load(open('out.json')); print(d['packages']['test']['issues'][0]['cve'][0])"
}

audit_raw_ucl_body() {
	setup_packages
	create_vuln_db

	# -R: raw UCL output (default format)
	atf_check \
		-o match:"pkg_count = 1" \
		-o match:"version.*1.5" \
		-o match:"CVE-2024-00001" \
		-s exit:1 \
		pkg audit -f vuln.xml -R
}

audit_pattern_body() {
	setup_packages

	# Also install a safe package
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "safe" "safe" "1.0" "/usr/local"
	atf_check -o ignore pkg register -M safe.ucl

	create_vuln_db

	# Audit only the vulnerable package by name
	atf_check \
		-o match:"1 problem" \
		-s exit:1 \
		pkg audit -f vuln.xml test

	# Audit only the safe package by name
	atf_check \
		-o match:"0 problem" \
		-s exit:0 \
		pkg audit -f vuln.xml safe
}

audit_multiple_vulns_body() {
	setup_packages

	# Two vulnerabilities affecting the same package
	cat > vuln.xml << 'EOF'
<?xml version="1.0" encoding="utf-8"?>
<vuxml xmlns="http://www.vuxml.org/apps/vuxml-1">
  <vuln vid="vuln-001">
    <topic>First vulnerability</topic>
    <affects>
      <package>
        <name>test</name>
        <range>
          <ge>1.0</ge>
          <lt>2.0</lt>
        </range>
      </package>
    </affects>
    <references>
      <cvename>CVE-2024-00001</cvename>
    </references>
  </vuln>
  <vuln vid="vuln-002">
    <topic>Second vulnerability</topic>
    <affects>
      <package>
        <name>test</name>
        <range>
          <ge>1.0</ge>
          <le>1.5</le>
        </range>
      </package>
    </affects>
    <references>
      <cvename>CVE-2024-00002</cvename>
    </references>
  </vuln>
</vuxml>
EOF

	# Both vulnerabilities should be reported
	atf_check \
		-o match:"CVE-2024-00001" \
		-o match:"CVE-2024-00002" \
		-o match:"2 problem" \
		-s exit:1 \
		pkg audit -f vuln.xml
}

audit_multiple_packages_body() {
	# Install two packages, only one vulnerable
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "vuln" "vuln" "1.0" "/usr/local"
	atf_check -o ignore pkg register -M vuln.ucl

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "safe" "safe" "1.0" "/usr/local"
	atf_check -o ignore pkg register -M safe.ucl

	cat > vuln.xml << 'EOF'
<?xml version="1.0" encoding="utf-8"?>
<vuxml xmlns="http://www.vuxml.org/apps/vuxml-1">
  <vuln vid="vuln-pkg-001">
    <topic>Vulnerability in vuln package</topic>
    <affects>
      <package>
        <name>vuln</name>
        <range>
          <le>2.0</le>
        </range>
      </package>
    </affects>
    <references>
      <cvename>CVE-2024-99999</cvename>
    </references>
  </vuln>
</vuxml>
EOF

	# Only vuln package should be flagged
	atf_check \
		-o match:"vuln-1.0 is vulnerable" \
		-o match:"1 problem.*1 package" \
		-s exit:1 \
		pkg audit -f vuln.xml

	# Quiet mode should only list the vulnerable one
	atf_check \
		-o inline:"vuln-1.0\n" \
		-s exit:1 \
		pkg audit -qf vuln.xml
}

audit_glob_name_body() {
	# Vuln DB uses a glob pattern for package name
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "perl5-DBI" "perl5-DBI" "1.5" "/usr/local"
	atf_check -o ignore pkg register -M perl5-DBI.ucl

	cat > vuln.xml << 'EOF'
<?xml version="1.0" encoding="utf-8"?>
<vuxml xmlns="http://www.vuxml.org/apps/vuxml-1">
  <vuln vid="perl-vuln-001">
    <topic>Vulnerability in perl DBI</topic>
    <affects>
      <package>
        <name>perl5*-DBI</name>
        <range>
          <lt>2.0</lt>
        </range>
      </package>
    </affects>
    <references>
      <cvename>CVE-2024-55555</cvename>
    </references>
  </vuln>
</vuxml>
EOF

	# Glob pattern in vuln DB should match perl5-DBI
	atf_check \
		-o match:"perl5-DBI-1.5 is vulnerable" \
		-o match:"CVE-2024-55555" \
		-s exit:1 \
		pkg audit -f vuln.xml
}

audit_no_db_body() {
	setup_packages

	# No vuln DB file -> error
	atf_check \
		-e match:"does not exist" \
		-s exit:1 \
		pkg audit -f /nonexistent/vuln.xml
}
