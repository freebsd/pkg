#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	requires

requires_body() {
	cat << EOF >> repo.conf
local1: {
	url: file://${TMPDIR},
	enabled: true
}
EOF

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg a a 1.0
	cat << EOF >> a.ucl
provides: [a-1]
EOF

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg b b 1.0
	cat << EOF >> b.ucl
requires: [a-1]
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
${JAILED}Fetching meta.txz:  done
${JAILED}Fetching packagesite.txz:  done
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
	    -e match:".*load error: access repo file.*" \
	    -s exit:1 \
	    pkg -o REPOS_DIR="${TMPDIR}" install -n b
}
