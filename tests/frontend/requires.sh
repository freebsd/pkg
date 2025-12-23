#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	requires \
	requires_del

requires_body() {
	mkdir reposconf
	cat << EOF >> reposconf/repo.conf
local1: {
	url: file://${TMPDIR},
	enabled: true
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg a a 1.0
	cat << EOF >> a.ucl
provides: [a-1]
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg b b 1.0
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
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data:  done
Processing entries:  done
local1 repository update completed. 2 packages processed.
All repositories are up to date.
Checking integrity... done (0 conflicting)
The following 2 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	a: 1.0
	b: 1.0

Number of packages to be installed: 2
"
	atf_check \
	    -o inline:"${OUTPUT}" \
	    -s exit:1 \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -n b

	atf_check \
	    -o ignore \
	    -s exit:0 \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y b

	atf_check \
		-o match:".*Nothing to do.*" \
		-s exit:0 \
		pkg autoremove -n
}

requires_del_body() {
	mkdir reposconf
	cat << EOF >> reposconf/repo.conf
local1: {
	url: file://${TMPDIR},
	enabled: true
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg a a 1.0
	cat << EOF >> a.ucl
provides: [a-1]
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg b b 1.0
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
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data:  done
Processing entries:  done
local1 repository update completed. 2 packages processed.
All repositories are up to date.
Checking integrity... done (0 conflicting)
The following 2 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	a: 1.0
	b: 1.0

Number of packages to be installed: 2
"
	atf_check \
	    -o inline:"${OUTPUT}" \
	    -s exit:1 \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -n b

	atf_check \
	    -o ignore \
	    -s exit:0 \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y b

	OUTPUT2="Checking integrity... done (0 conflicting)
Deinstallation has been requested for the following 2 packages (of 0 packages in the universe):

Installed packages to be REMOVED:
	a: 1.0
	b: 1.0

Number of packages to be removed: 2
"
	atf_check \
		-o inline:"${OUTPUT2}" \
		-s exit:0 \
		pkg delete -n a
}
