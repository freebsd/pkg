#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	lock \
	lock_delete \
	unlock_all

lock_setup() {
	for pkg in 'png' 'sqlite3' ; do
		atf_check \
		    -o match:".*Installing.*\.\.\.$" \
		    -e empty \
		    -s exit:0 \
		    pkg register -t -M ${RESOURCEDIR}/$pkg.ucl
	done

	test -f "./local.sqlite" || \
	    atf_fail "Can't populate $PKG_DBDIR/local.sqlite"
}

lock_head() {
	atf_set "require.files" \
	   "${RESOURCEDIR}/png.ucl ${RESOURCEDIR}/sqlite3.ucl"
}

lock_body() {
	lock_setup

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
	    -s exit:1 \
	    pkg lock -y sqlite3

	atf_check \
	    -o match:"Unlocking sqlite3.*" \
	    -e empty \
	    -s exit:0 \
	    pkg unlock -y sqlite3

	atf_check \
	    -o inline:"No locked packages were found\n" \
	    -e empty \
	    -s exit:1 \
	    pkg lock -l

	atf_check \
	    -o inline:"sqlite3-3.8.6: already unlocked\n" \
	    -e empty \
	    -s exit:1 \
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
	    -o inline:"No locked packages were found\n" \
	    -e empty \
	    -s exit:1 \
	    pkg lock -l
}

lock_delete_head() {
	lock_head
}

lock_delete_body() {
	lock_setup

	atf_check \
	    -o match:"Locking sqlite3.*" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -y sqlite3

	atf_check \
	    -o match:".*locked or vital and may not be removed.*" \
	    -o match:"sqlite3.*" \
	    -e empty \
	    -s exit:7 \
	    pkg delete -y sqlite3

	atf_check \
	    -o match:"sqlite3-3.8.6" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -l
}

unlock_all_body()
{
	mkdir target
	for i in "a" "b" "c" "d"; do
		atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "$i" "$i" "1" "prefix"
		atf_check -o ignore pkg register -M $i.ucl
	done
	atf_check -o ignore pkg lock -y a
	atf_check -o ignore pkg lock -y d
	atf_check -o ignore pkg unlock -ay
	atf_check -o inline:"No locked packages were found\n" \
		-s exit:1 pkg lock -l
}
