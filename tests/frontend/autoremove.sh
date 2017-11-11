#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	autoremove \
	autoremove_quiet \
	autoremove_dryrun

autoremove_prep() {
	touch file1
	touch file2

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 test 1
	cat << EOF >> pkg1.ucl
files: {
	${TMPDIR}/file1: "",
	${TMPDIR}/file2: "",
}
EOF

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg dep1 master 1
	cat << EOF >> dep1.ucl
deps: {
	test {
		origin: test,
		version: 1
	}
}
EOF

	atf_check \
	    -o match:".*Installing.*\.\.\.$" \
	    -e empty \
	    -s exit:0 \
	    pkg register -A -M pkg1.ucl

	atf_check \
	    -o match:".*Installing.*\.\.\.$" \
	    -e empty \
	    -s exit:0 \
	    pkg register -M dep1.ucl

	atf_check \
	    -o match:".*Deinstalling.*\.\.\.$" \
	    -e empty \
	    -s exit:0 \
	    pkg delete -y master
}

autoremove_body() {
	autoremove_prep

	atf_check \
	    -o match:"Deinstalling test-1\.\.\." \
	    -e empty \
	    -s exit:0 \
	    pkg autoremove -y

	atf_check \
	    -o empty \
	    -e empty \
	    -s exit:0 \
	    pkg info

	test ! -f ${TMPDIR}/file1 -o ! -f ${TMPDIR}/file2 || atf_fail "Files are still present"
}

autoremove_quiet_body() {
	autoremove_prep

	atf_check \
	    -o empty \
	    -e empty \
	    -s exit:0 \
	    pkg autoremove -yq

	atf_check \
	    -o empty \
	    -e empty \
	    -s exit:0 \
	    pkg info

	test ! -f ${TMPDIR}/file1 -o ! -f ${TMPDIR}/file2 || atf_fail "Files are still present"
}

autoremove_dryrun_body() {
	autoremove_prep

	atf_check \
	    -o match:"^Installed packages to be REMOVED:$" \
	    -o match:"^	test-1$" \
	    -e empty \
	    -s exit:0 \
	    pkg autoremove -yn

	atf_check \
	    -o match:"^test-1                         a test$" \
	    -e empty \
	    -s exit:0 \
	    pkg info

	test -f ${TMPDIR}/file1 -o -f ${TMPDIR}/file2 || atf_fail "Files are missing"
}
