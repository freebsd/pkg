#! /usr/bin/env atf-sh

atf_test_case lock
lock_head() {
	atf_set "descr" "pkg lock"
	atf_set "require.files" \
	   "${RESOURCEDIR}/png.ucl ${RESOURCEDIR}/sqlite3.ucl"
}

lock_body() {
	for pkg in 'png' 'sqlite3' ; do
		atf_check \
		    -o match:".*Installing.*\.\.\.$" \
		    -e empty \
		    -s exit:0 \
		    pkg register -t -M ${RESOURCEDIR}/$pkg.ucl
	done

	test -f "./local.sqlite" || \
	    atf_fail "Can't populate $PKG_DBDIR/local.sqlite"

	atf_check \
	    -o match:"Locking sqlite3.*" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -y sqlite3

	atf_check \
	    -o match:"sqlite3-3.8.6" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -l

	atf_check \
	    -o inline:"sqlite3-3.8.6: already locked\n" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -y sqlite3

	atf_check \
	    -o match:"Unlocking sqlite3.*" \
	    -e empty \
	    -s exit:0 \
	    pkg unlock -y sqlite3

	atf_check \
	    -o inline:"Currently locked packages:\n" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -l

	atf_check \
	    -o inline:"sqlite3-3.8.6: already unlocked\n" \
	    -e empty \
	    -s exit:0 \
	    pkg unlock -y sqlite3

	atf_check \
	    -o match:"Locking.*" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -y -a

	atf_check \
	    -o match:"sqlite3.*" \
	    -o match:"png.*" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -l

	atf_check \
	    -o match:"Unlocking.*" \
	    -e empty \
	    -s exit:0 \
	    pkg unlock -y -a

	atf_check \
	    -o inline:"Currently locked packages:\n" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -l
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/test_environment.sh

	# Tests are run in alphabetical order
	atf_add_test_case lock
}
