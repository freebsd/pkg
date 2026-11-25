#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	lock \
	lock_delete \
	lock_install_force \
	lock_upgrade_available \
	unlock_all \
	lock_already_locked \
	unlock_already_unlocked \
	lock_comment \
	lock_comment_not_exported

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

lock_comment_body() {
	lock_setup

	atf_check \
	    -o match:"Locking sqlite3.*" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -y -c "waiting for the sqlite4 migration" sqlite3

	atf_check \
	    -o inline:"Currently locked packages:\nsqlite3-3.8.6\n\twaiting for the sqlite4 migration\n" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -l

	# The comment of an already locked package is replaced.
	atf_check \
	    -o match:"sqlite3-3.8.6: already locked" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -y -c "another reason" sqlite3
	atf_check \
	    -o inline:"Currently locked packages:\nsqlite3-3.8.6\n\tanother reason\n" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -l

	# The comment goes away with the lock.
	atf_check \
	    -o match:"Unlocking sqlite3.*" \
	    -e empty \
	    -s exit:0 \
	    pkg unlock -y sqlite3
	atf_check \
	    -o match:"Locking sqlite3.*" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -y sqlite3
	atf_check \
	    -o inline:"Currently locked packages:\nsqlite3-3.8.6\n" \
	    -e empty \
	    -s exit:0 \
	    pkg lock -l
}

# The lock comment describes the state of the local database, it must not be
# recorded in a package built from the installed package.
lock_comment_not_exported_body() {
	atf_check touch ${TMPDIR}/lockfile
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "lck" "lck" "1"
	cat << EOF >> lck.ucl
files: {
	${TMPDIR}/lockfile: "",
}
EOF
	atf_check -o ignore pkg create -M lck.ucl
	atf_check -o ignore \
	    pkg -o REPOS_DIR=/dev/null install -qy ${TMPDIR}/lck-1.pkg
	atf_check -o match:"Locking lck.*" -e empty -s exit:0 \
	    pkg lock -y -c "local reason" lck

	# Rebuild the package from the installed one.
	atf_check -o ignore pkg create -o ${TMPDIR} lck

	# Installing it elsewhere must not bring the comment along.
	atf_check mkdir ${TMPDIR}/other
	atf_check -o ignore \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/other \
	        install -qy ${TMPDIR}/lck-1.pkg
	atf_check -o not-match:"lock_comment" \
	    pkg -r ${TMPDIR}/other info -A lck
}
