#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	formula

formula_body() {
	cat << EOF >> repo.conf
local1: {
	url: file://${TMPDIR},
	enabled: true
}
EOF

	cat << EOF > a.ucl
name: a
origin: a
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
EOF

	cat << EOF > b.ucl
name: b
origin: b
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
dep_formula: 'a >= 1',
EOF
	for p in a b; do
		atf_check \
		    -o ignore \
		    -e empty \
		    -s exit:0 \
		    pkg create -M ./${p}.ucl
	done

	atf_check \
	    -o ignore \
	    -e empty \
	    -s exit:0 \
	    pkg repo .

	OUTPUT="Updating local1 repository catalogue...
${JAILED}meta.txz                                 :  done
${JAILED}packagesite.txz                          :  done
Processing entries:  done
local1 repository update completed. 2 packages processed.
All repositories are up to date.
Checking integrity... done (0 conflicting)
The following 2 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	b: 1.0
	a: 1.0

Number of packages to be installed: 2
"
	atf_check \
	    -o inline:"${OUTPUT}" \
	    -s exit:1 \
	    pkg -o REPOS_DIR="${TMPDIR}" install -n b
}
