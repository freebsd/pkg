#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	lock \
	lock_delete \
	lock_install_force \
	lock_upgrade_available \
	unlock_all \
	lock_already_locked \
	unlock_already_unlocked

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
	    -s exit:0 \
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

lock_install_force_body() {
	# Regression test for issue #2151: pkg install -f on a locked package
	# should warn that the package is locked instead of silently doing
	# nothing or printing a misleading message.

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh \
		new_pkg test test 1 /usr/local

	# Create package and repo
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	# Install the package
	atf_check \
		-o ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" \
		install -y test

	# Lock it
	atf_check -o ignore -s exit:0 pkg lock -y test

	# pkg install -f should warn about the lock and fail
	atf_check \
		-o match:"locked" \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" \
		install -fy test

	# Package should still be installed
	atf_check -s exit:0 pkg info -e test
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

lock_already_locked_body()
{
	# Locking an already locked package should succeed (idempotent).
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check -o ignore pkg register -M test.ucl

	atf_check -o ignore -s exit:0 pkg lock -y test

	# Second lock: should succeed with informational message
	atf_check \
		-o inline:"test-1: already locked\n" \
		-e empty \
		-s exit:0 \
		pkg lock -y test

	# Quiet mode: no output at all, still success
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg lock -qy test

	# Package should remain locked
	atf_check \
		-o match:"test-1" \
		-s exit:0 \
		pkg lock -l
}

unlock_already_unlocked_body()
{
	# Unlocking an already unlocked package should succeed (idempotent).
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check -o ignore pkg register -M test.ucl

	# Package is unlocked by default; unlock should succeed
	atf_check \
		-o inline:"test-1: already unlocked\n" \
		-e empty \
		-s exit:0 \
		pkg unlock -y test

	# Quiet mode: no output at all, still success
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg unlock -qy test
}

# A locked package which the repository can upgrade must be reported as locked
# along with the version available, instead of pretending that it is up to
# date (issue #2698).
lock_upgrade_available_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh \
		new_pkg test test 1 /usr/local
	atf_check -o ignore pkg create -M test.ucl
	atf_check -o ignore pkg repo .

	mkdir reposconf
	cat <<EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		-o PKG_CACHEDIR="${TMPDIR}" update
	atf_check -o ignore pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		-o PKG_CACHEDIR="${TMPDIR}" install -y test
	atf_check -o ignore pkg lock -y test

	# A newer version is published.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh \
		new_pkg test test 2 /usr/local
	atf_check -o ignore pkg create -M test.ucl
	sleep 1
	atf_check -o ignore pkg repo .
	atf_check -o ignore pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		-o PKG_CACHEDIR="${TMPDIR}" update -f

	atf_check \
		-o match:"test-1 is locked and may not be modified \(2 is available\)" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		-o PKG_CACHEDIR="${TMPDIR}" upgrade -n
}
