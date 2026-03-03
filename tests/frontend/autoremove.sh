#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	autoremove \
	autoremove_quiet \
	autoremove_dryrun \
	autoremove_order

autoremove_prep() {
	touch file1
	touch file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 test 1
	cat << EOF >> pkg1.ucl
files: {
	${TMPDIR}/file1: "",
	${TMPDIR}/file2: "",
}
scripts: {
	post-deinstall: "exit 1"
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg dep1 master 1
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
	    -e inline:"${PROGNAME}: POST-DEINSTALL script failed\n" \
	    -s exit:0 \
	    pkg autoremove -y

	atf_check \
	    -o empty \
	    -e empty \
	    -s exit:0 \
	    pkg info

	test ! -f ${TMPDIR}/file1 -o ! -f ${TMPDIR}/file2 || atf_fail "Files are still present"
}

autoremove_no_scripts_body() {
	autoremove_prep

	atf_check \
	    -o match:"Deinstalling test-1\.\.\." \
	    -e empty \
	    -s exit:0 \
	    pkg autoremove -yDq

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
	    -e inline:"${PROGNAME}: POST-DEINSTALL script failed\n" \
	    -s exit:0 \
	    pkg autoremove -yq

	atf_check \
	    -o empty \
	    -e empty \
	    -s exit:0 \
	    pkg info

	test ! -f ${TMPDIR}/file1 -o ! -f ${TMPDIR}/file2 || atf_fail "Files are still present"
}

autoremove_order_body() {
	# Create a dependency chain: top -> libc -> libb -> liba
	# liba, libb, libc are automatic; top is manual.
	# After removing top, autoremove should remove in dependency
	# order: libc first (leaf), then libb, then liba (base).

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg liba liba 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg libb libb 1
	cat << EOF >> libb.ucl
deps: {
	liba {
		origin: liba,
		version: 1
	}
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg libc libc 1
	cat << EOF >> libc.ucl
deps: {
	libb {
		origin: libb,
		version: 1
	}
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg top top 1
	cat << EOF >> top.ucl
deps: {
	libc {
		origin: libc,
		version: 1
	}
}
EOF

	atf_check -o ignore -s exit:0 pkg register -A -M liba.ucl
	atf_check -o ignore -s exit:0 pkg register -A -M libb.ucl
	atf_check -o ignore -s exit:0 pkg register -A -M libc.ucl
	atf_check -o ignore -s exit:0 pkg register -M top.ucl

	atf_check -o ignore -s exit:0 pkg delete -y top

	# Run autoremove and capture output
	pkg autoremove -y > output.txt 2>/dev/null || atf_fail "pkg autoremove failed"

	# Verify removal order: dependents before their dependencies
	grep "Deinstalling" output.txt | sed 's/.*Deinstalling //;s/\.\.\.//' > order.txt
	printf "libc-1\nlibb-1\nliba-1\n" > expected.txt
	diff -u expected.txt order.txt || atf_fail "Wrong autoremove order"

	# All packages should be gone
	atf_check \
	    -o empty \
	    -e empty \
	    -s exit:0 \
	    pkg info
}

autoremove_dryrun_body() {
	autoremove_prep

	atf_check \
	    -o match:"^Installed packages to be REMOVED:$" \
	    -o match:"^	test: 1$" \
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
