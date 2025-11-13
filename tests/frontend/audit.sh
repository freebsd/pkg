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
	cat > vuln.json << 'EOF'
[
    {
    "affected": [
        {
        "package": {
            "ecosystem": "FreeBSD:ports",
            "name": "test"
        },
        "ranges": [
            {
            "events": [
                {
                "introduced": "1.0"
                },
                {
                "fixed": "2.0"
                }
            ],
            "type": "ECOSYSTEM"
            }
        ]
        }
    ],
    "database_specific": {
        "discovery": "2024-01-01T00:00:00Z",
        "references": {
        "cvename": [
            "CVE-2024-00001"
        ]
        },
        "vid": "test-vuln-001"
    },
    "details": "Example description\n",
    "id": "FreeBSD-2024-0001",
    "modified": "2024-01-01T00:00:00Z",
    "published": "2024-01-01T00:00:00Z",
    "references": [
        {
        "type": "ADVISORY",
        "url": "https://cveawg.mitre.org/api/cve/CVE-2024-00001"
        }
    ],
    "schema_version": "1.7.0",
    "summary": "Test vulnerability in test package"
    }
]
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
		pkg audit -f vuln.json
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
		pkg audit -f vuln.json
}

audit_empty_db_body() {
	setup_packages

	cat > vuln.json << 'EOF'
[]
EOF

	# Empty vuln db -> no problems
	atf_check \
		-o match:"0 problem" \
		-s exit:0 \
		pkg audit -f vuln.json
}

audit_out_of_range_body() {
	setup_packages

	# Vulnerability only affects versions < 1.0
	cat > vuln.json << 'EOF'
[
    {
    "affected": [
        {
        "package": {
            "ecosystem": "FreeBSD:ports",
            "name": "test"
        },
        "ranges": [
            {
            "events": [
                {
                "introduced": "0"
                },
                {
                "fixed": "1.0"
                }
            ],
            "type": "ECOSYSTEM"
            }
        ]
        }
    ],
    "database_specific": {
        "discovery": "2020-12-31T00:00:00Z",
        "references": {
        "cvename": [
            "CVE-2020-99999"
        ]
        },
        "vid": "old-vuln"
    },
    "details": "Old vulnerability description\n",
    "id": "FreeBSD-2020-0001",
    "modified": "2020-12-31T00:00:00Z",
    "published": "2020-12-31T00:00:00Z",
    "references": [
        {
        "type": "ADVISORY",
        "url": "https://cveawg.mitre.org/api/cve/CVE-2020-99999"
        }
    ],
    "schema_version": "1.7.0",
    "summary": "Old vulnerability"
    }
]
EOF

	# test-1.5 >= 1.0, so not affected
	atf_check \
		-o match:"0 problem" \
		-s exit:0 \
		pkg audit -f vuln.json
}

audit_quiet_body() {
	setup_packages
	create_vuln_db

	# -q: quiet mode shows only package name-version
	atf_check \
		-o inline:"test-1.5\n" \
		-s exit:1 \
		pkg audit -qf vuln.json
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
		pkg audit -rf vuln.json
}

audit_raw_json_body() {
	atf_require python3 "Requires python3 to run this test"
	setup_packages
	create_vuln_db

	# -Rjson: raw JSON output
	atf_check \
		-o save:out.json \
		-s exit:1 \
		pkg audit -f vuln.json -Rjson

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
		pkg audit -f vuln.json -R
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
		pkg audit -f vuln.json test

	# Audit only the safe package by name
	atf_check \
		-o match:"0 problem" \
		-s exit:0 \
		pkg audit -f vuln.json safe
}

audit_multiple_vulns_body() {
	setup_packages

	# Two vulnerabilities affecting the same package
	cat > vuln.json << 'EOF'
[
    {
        "affected": [
            {
                "package": {
                    "ecosystem": "FreeBSD:ports",
                    "name": "test"
                },
                "ranges": [
                    {
                        "events": [
                            {
                                "introduced": "1.0"
                            },
                            {
                                "fixed": "1.5"
                            }
                        ],
                        "type": "ECOSYSTEM"
                    }
                ]
            }
        ],
        "database_specific": {
            "discovery": "2024-01-02T00:00:00Z",
            "references": {
                "cvename": [
                    "CVE-2024-00002"
                ]
            },
            "vid": "vuln-002"
        },
        "details": "Second vulnerability description\n",
        "id": "FreeBSD-2024-0003",
        "modified": "2024-01-02T00:00:00Z",
        "published": "2024-01-02T00:00:00Z",
        "references": [
            {
                "type": "ADVISORY",
                "url": "https://cveawg.mitre.org/api/cve/CVE-2024-00002"
            }
        ],
        "schema_version": "1.7.0",
        "summary": "Second vulnerability"
    },
    {
        "affected": [
            {
                "package": {
                    "ecosystem": "FreeBSD:ports",
                    "name": "test"
                },
                "ranges": [
                    {
                        "events": [
                            {
                                "introduced": "1.0"
                            },
                            {
                                "fixed": "2.0"
                            }
                        ],
                        "type": "ECOSYSTEM"
                    }
                ]
            }
        ],
        "database_specific": {
            "discovery": "2024-01-01T00:00:00Z",
            "references": {
                "cvename": [
                    "CVE-2024-00001"
                ]
            },
            "vid": "vuln-001"
        },
        "details": "First vulnerability description\n",
        "id": "FreeBSD-2024-0004",
        "modified": "2024-01-01T00:00:00Z",
        "published": "2024-01-01T00:00:00Z",
        "references": [
            {
                "type": "ADVISORY",
                "url": "https://cveawg.mitre.org/api/cve/CVE-2024-00001"
            }
        ],
        "schema_version": "1.7.0",
        "summary": "First vulnerability"
    }
]
EOF

	# Both vulnerabilities should be reported
	atf_check \
		-o match:"CVE-2024-00001" \
		-o match:"CVE-2024-00002" \
		-o match:"2 problem" \
		-s exit:1 \
		pkg audit -f vuln.json
}

audit_multiple_packages_body() {
	# Install two packages, only one vulnerable
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "vuln" "vuln" "1.0" "/usr/local"
	atf_check -o ignore pkg register -M vuln.ucl

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "safe" "safe" "1.0" "/usr/local"
	atf_check -o ignore pkg register -M safe.ucl

	cat > vuln.json << 'EOF'
[
    {
        "affected": [
            {
                "package": {
                    "ecosystem": "FreeBSD:ports",
                    "name": "vuln"
                },
                "ranges": [
                    {
                        "events": [
                            {
                                "introduced": "0"
                            },
                            {
                                "fixed": "2.0"
                            }
                        ],
                        "type": "ECOSYSTEM"
                    }
                ]
            }
        ],
        "database_specific": {
            "discovery": "2024-12-31T00:00:00Z",
            "references": {
                "cvename": [
                    "CVE-2024-99999"
                ]
            },
            "vid": "vuln-pkg-001"
        },
        "details": "Vulnerability in vuln package description\n",
        "id": "FreeBSD-2024-0002",
        "modified": "2024-12-31T00:00:00Z",
        "published": "2024-12-31T00:00:00Z",
        "references": [
            {
                "type": "ADVISORY",
                "url": "https://cveawg.mitre.org/api/cve/CVE-2024-99999"
            }
        ],
        "schema_version": "1.7.0",
        "summary": "Vulnerability in vuln package"
    }
]
EOF

	# Only vuln package should be flagged
	atf_check \
		-o match:"vuln-1.0 is vulnerable" \
		-o match:"1 problem.*1 package" \
		-s exit:1 \
		pkg audit -f vuln.json

	# Quiet mode should only list the vulnerable one
	atf_check \
		-o inline:"vuln-1.0\n" \
		-s exit:1 \
		pkg audit -qf vuln.json
}

audit_glob_name_body() {
	# Vuln DB uses a glob pattern for package name
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "perl5-DBI" "perl5-DBI" "1.5" "/usr/local"
	atf_check -o ignore pkg register -M perl5-DBI.ucl

	cat > vuln.json << 'EOF'
[
    {
        "affected": [
            {
                "package": {
                    "ecosystem": "FreeBSD:ports",
                    "name": "perl5-DBI"
                },
                "ranges": [
                    {
                        "events": [
                            {
                                "introduced": "0"
                            },
                            {
                                "fixed": "2.0"
                            }
                        ],
                        "type": "ECOSYSTEM"
                    }
                ]
            }
        ],
        "database_specific": {
            "discovery": "2024-06-15T00:00:00Z",
            "references": {
                "cvename": [
                    "CVE-2024-55555"
                ]
            },
            "vid": "perl-vuln-001"
        },
        "details": "Vulnerability in perl DBI description\n",
        "id": "FreeBSD-2024-0001",
        "modified": "2024-06-15T00:00:00Z",
        "published": "2024-06-15T00:00:00Z",
        "references": [
            {
                "type": "ADVISORY",
                "url": "https://cveawg.mitre.org/api/cve/CVE-2024-55555"
            }
        ],
        "schema_version": "1.7.0",
        "summary": "Vulnerability in perl DBI"
    }
]
EOF

	# Glob pattern in vuln DB should match perl5-DBI
	atf_check \
		-o match:"perl5-DBI-1.5 is vulnerable" \
		-o match:"CVE-2024-55555" \
		-s exit:1 \
		pkg audit -f vuln.json
}

audit_no_db_body() {
	setup_packages

	# No vuln DB file -> error
	atf_check \
		-e match:"does not exist" \
		-s exit:1 \
		pkg audit -f /nonexistent/vuln.json
}
