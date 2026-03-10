#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	delete_all \
	delete_pkg \
	delete_with_directory_owned \
	simple_delete \
	simple_delete_prefix_ending_with_slash \
	delete_force_ignores_rdeps \
	delete_without_force_pulls_rdeps \
	delete_dry_run \
	delete_quiet \
	delete_glob \
	delete_regex \
	delete_nonexistent \
	delete_locked \
	delete_multiple \
	delete_no_scripts \
	delete_all_preserves_pkg \
	delete_recursive_force \
	delete_dry_run_no_remove

delete_all_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkg" "pkg" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	atf_check -o ignore pkg register -M foo.ucl
	atf_check -o ignore pkg register -M pkg.ucl
	atf_check -o ignore pkg register -M test.ucl

	atf_check -o ignore pkg delete -ay
}

delete_pkg_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkg" "pkg" "1"
	atf_check -o ignore pkg register -M pkg.ucl
	atf_check -o ignore -e ignore -s exit:3 pkg delete -y pkg
	atf_check -o ignore -e ignore pkg delete -yf pkg
}

simple_delete_body() {
	touch file1
	mkdir dir
	touch dir/file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir/file2: "",
}
EOF

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg delete -y test

	test -f file1 && atf_fail "'file1' still present"
	test -f dir/file2 && atf_fail "'dir/file2' still present"
	test -d dir && atf_fail "'dir' still present"
	test -d ${TMPDIR} || atf_fail "Prefix have been removed"
}

simple_delete_prefix_ending_with_slash_body() {
	touch file1
	mkdir dir
	touch dir/file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}/"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir/file2: "",
}
EOF

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg delete -y test

	test -f file1 && atf_fail "'file1' still present"
	test -f dir/file2 && atf_fail "'dir/file2' still present"
	test -d dir && atf_fail "'dir' still present"
	test -d ${TMPDIR} || atf_fail "Prefix have been removed"
}

delete_force_ignores_rdeps_body() {
	# B depends on A. "pkg delete -fy A" should only delete A, not B.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgA" "pkgA" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgB" "pkgB" "1"
	cat << EOF >> pkgB.ucl
deps: {
	pkgA {
		origin: pkgA,
		version: "1"
	}
}
EOF

	atf_check -o ignore pkg register -M pkgA.ucl
	atf_check -o ignore pkg register -M pkgB.ucl

	# Force delete pkgA: should succeed and not pull in pkgB
	atf_check \
		-o match:"Deinstalling pkgA" \
		-o not-match:"pkgB" \
		-s exit:0 \
		pkg delete -yf pkgA

	# pkgB should still be installed
	atf_check \
		-o match:"pkgB" \
		-s exit:0 \
		pkg info
}

delete_without_force_pulls_rdeps_body() {
	# B depends on A. "pkg delete A" (no -f) should also schedule B for removal.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgA" "pkgA" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgB" "pkgB" "1"
	cat << EOF >> pkgB.ucl
deps: {
	pkgA {
		origin: pkgA,
		version: "1"
	}
}
EOF

	atf_check -o ignore pkg register -M pkgA.ucl
	atf_check -o ignore pkg register -M pkgB.ucl

	# Delete pkgA without force: should also remove pkgB (rdep)
	atf_check \
		-o match:"Deinstalling pkgA" \
		-o match:"Deinstalling pkgB" \
		-s exit:0 \
		pkg delete -y pkgA

	# Both should be gone
	atf_check \
		-o not-match:"pkgA" \
		-o not-match:"pkgB" \
		-s exit:0 \
		pkg info
}

delete_with_directory_owned_body() {
	touch file1
	mkdir dir
	touch dir/file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}/"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir/file2: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test2" "test2" "1" "${TMPDIR}/"
	cat << EOF >> test2.ucl
directories: {
    ${TMPDIR}/dir: 'y',
}
EOF
	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M test2.ucl

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg delete -y test

	test -f file1 && atf_fail "'file1' still present"
	test -f dir/file2 && atf_fail "'dir/file2' still present"
	test -d dir || atf_fail "'dir' has been removed"

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg delete -y test2

	test -d dir && atf_fail "'dir' still present"
	test -d ${TMPDIR} || atf_fail "Prefix has been removed"
}

delete_dry_run_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	touch file1
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
}
EOF
	atf_check -o ignore pkg register -M test.ucl

	# -n: dry run should show what would be removed
	atf_check \
		-o match:"REMOVED" \
		-o match:"test" \
		-s exit:0 \
		pkg delete -n test

	# Package should still be installed
	atf_check \
		-s exit:0 \
		pkg info -e test
}

delete_dry_run_no_remove_body() {
	touch file1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
}
EOF
	atf_check -o ignore pkg register -M test.ucl

	# Dry run should not remove files
	atf_check -o ignore -s exit:0 pkg delete -ny test

	test -f file1 || atf_fail "'file1' was removed during dry run"

	# Package should still be queryable
	atf_check \
		-o match:"test-1" \
		-s exit:0 \
		pkg info test
}

delete_quiet_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check -o ignore pkg register -M test.ucl

	# -q: quiet mode should suppress output
	atf_check \
		-o empty \
		-s exit:0 \
		pkg delete -yq test

	# Package should be gone
	atf_check \
		-s exit:1 \
		pkg info -e test
}

delete_glob_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "libfoo" "libfoo" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "libbar" "libbar" "2"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "baz" "baz" "3"

	atf_check -o ignore pkg register -M libfoo.ucl
	atf_check -o ignore pkg register -M libbar.ucl
	atf_check -o ignore pkg register -M baz.ucl

	# -g: glob match should delete lib* only
	atf_check \
		-o match:"Deinstalling libfoo" \
		-o match:"Deinstalling libbar" \
		-s exit:0 \
		pkg delete -yfg 'lib*'

	# baz should remain
	atf_check -s exit:0 pkg info -e baz

	# lib* should be gone
	atf_check -s exit:1 pkg info -e libfoo
	atf_check -s exit:1 pkg info -e libbar
}

delete_regex_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "libfoo" "libfoo" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "libbar" "libbar" "2"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "baz" "baz" "3"

	atf_check -o ignore pkg register -M libfoo.ucl
	atf_check -o ignore pkg register -M libbar.ucl
	atf_check -o ignore pkg register -M baz.ucl

	# -x: regex match
	atf_check \
		-o match:"Deinstalling libfoo" \
		-o match:"Deinstalling libbar" \
		-o not-match:"baz" \
		-s exit:0 \
		pkg delete -yx '^lib'

	# baz should remain
	atf_check -s exit:0 pkg info -e baz
	atf_check -s exit:1 pkg info -e libfoo
	atf_check -s exit:1 pkg info -e libbar
}

delete_nonexistent_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check -o ignore pkg register -M test.ucl

	# Deleting a package that does not exist
	atf_check \
		-o match:"No packages matched" \
		-s exit:1 \
		pkg delete -y nosuchpkg
}

delete_locked_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check -o ignore pkg register -M test.ucl

	# Lock the package
	atf_check -o ignore -s exit:0 pkg lock -y test

	# Attempt to delete locked package should fail (exit 7 = EPKG_LOCKED)
	atf_check \
		-o match:"locked" \
		-s exit:7 \
		pkg delete -y test

	# Package should still be installed
	atf_check -s exit:0 pkg info -e test

	# Unlock and delete should work
	atf_check -o ignore -s exit:0 pkg unlock -y test
	atf_check -o ignore -s exit:0 pkg delete -y test
	atf_check -s exit:1 pkg info -e test
}

delete_multiple_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "2"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "baz" "baz" "3"

	atf_check -o ignore pkg register -M foo.ucl
	atf_check -o ignore pkg register -M bar.ucl
	atf_check -o ignore pkg register -M baz.ucl

	# Delete two packages at once, third should remain
	atf_check \
		-o match:"Deinstalling foo" \
		-o match:"Deinstalling bar" \
		-o not-match:"baz" \
		-s exit:0 \
		pkg delete -y foo bar

	atf_check -s exit:1 pkg info -e foo
	atf_check -s exit:1 pkg info -e bar
	atf_check -s exit:0 pkg info -e baz
}

delete_no_scripts_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
scripts: {
    pre-deinstall: "echo PRESCRIPT >> ${TMPDIR}/scriptlog",
    post-deinstall: "echo POSTSCRIPT >> ${TMPDIR}/scriptlog",
}
EOF
	atf_check -o ignore pkg register -M test.ucl

	# -D: skip scripts
	atf_check \
		-o match:"Deinstalling test" \
		-s exit:0 \
		pkg delete -yD test

	# Script log should not exist (scripts were not run)
	test -f ${TMPDIR}/scriptlog && atf_fail "Scripts were executed with -D"
	return 0
}

delete_all_preserves_pkg_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkg" "pkg" "1"

	atf_check -o ignore pkg register -M foo.ucl
	atf_check -o ignore pkg register -M bar.ucl
	atf_check -o ignore pkg register -M pkg.ucl

	# -a without -f should preserve pkg itself
	atf_check -o ignore -s exit:0 pkg delete -ay

	# pkg should still be installed
	atf_check -s exit:0 pkg info -e pkg

	# others should be gone
	atf_check -s exit:1 pkg info -e foo
	atf_check -s exit:1 pkg info -e bar
}

delete_recursive_force_body() {
	# A depends on B depends on C. Default delete is recursive.
	# -f forces non-recursive even with -R.
	# Verify that -f only deletes the target, leaving rdeps.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgC" "pkgC" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgB" "pkgB" "1"
	cat << EOF >> pkgB.ucl
deps: {
	pkgC {
		origin: pkgC,
		version: "1"
	}
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgA" "pkgA" "1"
	cat << EOF >> pkgA.ucl
deps: {
	pkgB {
		origin: pkgB,
		version: "1"
	}
}
EOF

	atf_check -o ignore pkg register -M pkgC.ucl
	atf_check -o ignore pkg register -M pkgB.ucl
	atf_check -o ignore pkg register -M pkgA.ucl

	# -f: force non-recursive, only pkgC is removed
	atf_check \
		-o match:"Deinstalling pkgC" \
		-o not-match:"pkgB" \
		-o not-match:"pkgA" \
		-s exit:0 \
		pkg delete -yf pkgC

	# pkgA and pkgB should still be installed
	atf_check -s exit:0 pkg info -e pkgA
	atf_check -s exit:0 pkg info -e pkgB
	atf_check -s exit:1 pkg info -e pkgC
}
