#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	raw_json_single \
	raw_json_multiple \
	raw_json_compact_multiple \
	raw_json_all \
	info_all \
	info_full \
	info_name_only \
	info_comment \
	info_deps \
	info_rdeps \
	info_files \
	info_size \
	info_origin \
	info_prefix \
	info_quiet \
	info_exists \
	info_not_found \
	info_no_args \
	info_glob \
	info_regex \
	info_locked \
	info_options \
	info_pkg_file

# Helper: register a rich test package with deps, files, options
setup_pkg() {
	touch file1 file2
	mkdir -p mydir

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "dep" "dep" "1.0" "${TMPDIR}"
	atf_check -o ignore -s exit:0 pkg register -M dep.ucl

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2.5" "${TMPDIR}"
	cat << EOF >> test.ucl
deps: {
    dep: {
        origin: dep,
        version: "1.0"
    }
}
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/file2: "",
}
directories: {
    ${TMPDIR}/mydir: "",
}
options: {
    "OPT1": "on",
    "OPT2": "off",
}
EOF
	atf_check -o ignore -s exit:0 pkg register -M test.ucl
}

info_all_body() {
	setup_pkg

	# pkg info -a lists all installed packages
	atf_check \
	    -o match:"dep-1.0" \
	    -o match:"test-2.5" \
	    -s exit:0 \
	    pkg info -a

	# quiet mode
	atf_check \
	    -o match:"dep-1.0" \
	    -o match:"test-2.5" \
	    -s exit:0 \
	    pkg info -qa
}

info_full_body() {
	setup_pkg

	# Single package with no flags → full info
	atf_check \
	    -o match:"Name" \
	    -o match:"Version" \
	    -o match:"Comment" \
	    -o match:"Prefix" \
	    -s exit:0 \
	    pkg info test
}

info_name_only_body() {
	setup_pkg

	# -E: show name only (exit 0 if exists)
	atf_check \
	    -o match:"test" \
	    -s exit:0 \
	    pkg info -E test
}

info_comment_body() {
	setup_pkg

	# -I: show comment
	atf_check \
	    -o match:"a test" \
	    -s exit:0 \
	    pkg info -I test
}

info_deps_body() {
	setup_pkg

	# -d: show deps
	atf_check \
	    -o match:"dep-1.0" \
	    -s exit:0 \
	    pkg info -d test
}

info_rdeps_body() {
	setup_pkg

	# -r: show reverse deps
	atf_check \
	    -o match:"test-2.5" \
	    -s exit:0 \
	    pkg info -r dep
}

info_files_body() {
	setup_pkg

	# -l: list files
	atf_check \
	    -o match:"${TMPDIR}/file1" \
	    -o match:"${TMPDIR}/file2" \
	    -s exit:0 \
	    pkg info -l test

	# quiet mode
	atf_check \
	    -o match:"${TMPDIR}/file1" \
	    -s exit:0 \
	    pkg info -ql test
}

info_size_body() {
	setup_pkg

	# -s: show flat size
	atf_check \
	    -o match:"0.00B" \
	    -s exit:0 \
	    pkg info -s test
}

info_origin_body() {
	setup_pkg

	# -o: show origin
	atf_check \
	    -o match:"test" \
	    -s exit:0 \
	    pkg info -o test
}

info_prefix_body() {
	setup_pkg

	# -p: show prefix
	atf_check \
	    -o match:"${TMPDIR}" \
	    -s exit:0 \
	    pkg info -p test
}

info_quiet_body() {
	setup_pkg

	# -q with -a: just name-version
	OUTPUT=$(pkg info -qa)
	case "$OUTPUT" in
	*"Name"*|*"Comment"*|*"Prefix"*)
		atf_fail "Quiet mode should not show field labels" ;;
	esac

	# Should still list packages
	echo "$OUTPUT" | grep -q "test-2.5" || atf_fail "test-2.5 not listed"
	echo "$OUTPUT" | grep -q "dep-1.0" || atf_fail "dep-1.0 not listed"
}

info_exists_body() {
	setup_pkg

	# -e: exit 0 if package exists
	atf_check \
	    -s exit:0 \
	    pkg info -e test

	# exit 1 if not
	atf_check \
	    -s exit:1 \
	    pkg info -e nonexistent
}

info_not_found_body() {
	setup_pkg

	# Non-existent package
	atf_check \
	    -e match:"No package.*matching" \
	    -s exit:1 \
	    pkg info nosuchpkg

	# Quiet: no stderr output
	atf_check \
	    -e empty \
	    -s exit:1 \
	    pkg info -q nosuchpkg
}

info_no_args_body() {
	# No packages installed, no args → lists nothing
	atf_check \
	    -o empty \
	    -s exit:0 \
	    pkg info
}

info_glob_body() {
	setup_pkg

	# -g: glob matching
	atf_check \
	    -o match:"test-2.5" \
	    -o not-match:"dep-1.0" \
	    -s exit:0 \
	    pkg info -g 'tes*'

	# Match both
	atf_check \
	    -o match:"test-2.5" \
	    -o match:"dep-1.0" \
	    -s exit:0 \
	    pkg info -g '*'

	# No match
	atf_check \
	    -e match:"No package" \
	    -s exit:1 \
	    pkg info -g 'zzz*'
}

info_regex_body() {
	setup_pkg

	# -x: regex matching
	atf_check \
	    -o match:"test-2.5" \
	    -o not-match:"dep-1.0" \
	    -s exit:0 \
	    pkg info -x '^tes'

	# Match both
	atf_check \
	    -o match:"test-2.5" \
	    -o match:"dep-1.0" \
	    -s exit:0 \
	    pkg info -x '.*'
}

info_locked_body() {
	setup_pkg

	# Lock a package, then info -k should show lock status
	atf_check -o ignore -s exit:0 pkg lock -y test

	atf_check \
	    -o match:"yes" \
	    -s exit:0 \
	    pkg info -k test

	atf_check -o ignore -s exit:0 pkg unlock -y test
}

info_options_body() {
	setup_pkg

	# -O: show options (registered via manifest)
	atf_check \
	    -o match:"OPT1" \
	    -o match:"OPT2" \
	    -s exit:0 \
	    pkg info -f test
}

info_pkg_file_body() {
	# -F: info from a .pkg file directly
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "filepkg" "filepkg" "3.0" "/"
	touch a
	echo ${TMPDIR}/a > plist
	atf_check -o ignore pkg create -M filepkg.ucl -p plist

	atf_check \
	    -o match:"filepkg" \
	    -o match:"3.0" \
	    -s exit:0 \
	    pkg info -F filepkg-3.0.pkg

	# -l on a file
	atf_check \
	    -o match:"${TMPDIR}/a" \
	    -s exit:0 \
	    pkg info -lF filepkg-3.0.pkg
}

raw_json_single_body() {
	atf_require python3 "Requires python3 to run this test"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgA" "pkgA" "1.0" "/"
	touch a
	echo ${TMPDIR}/a > plist
	atf_check pkg create -M pkgA.ucl -p plist
	atf_check -o ignore pkg add pkgA-1.0.pkg
	# Single package raw JSON should be valid JSON (array with one element)
	atf_check -o save:out.json pkg info --raw --raw-format json pkgA
	atf_check -o ignore -e empty python3 -m json.tool out.json
}

raw_json_multiple_body() {
	atf_require python3 "Requires python3 to run this test"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgA" "pkgA" "1.0" "/"
	touch a
	echo ${TMPDIR}/a > plist
	atf_check pkg create -M pkgA.ucl -p plist
	atf_check -o ignore pkg add pkgA-1.0.pkg

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgB" "pkgB" "2.0" "/"
	touch b
	echo ${TMPDIR}/b > plist
	atf_check pkg create -M pkgB.ucl -p plist
	atf_check -o ignore pkg add pkgB-2.0.pkg

	# Two packages raw JSON must be a valid JSON array
	atf_check -o save:out.json pkg info --raw --raw-format json pkgA pkgB
	atf_check -o ignore -e empty python3 -m json.tool out.json
	# Verify it is a JSON array with 2 elements
	atf_check -o inline:"2\n" python3 -c "import json; print(len(json.load(open('out.json'))))"
}

raw_json_compact_multiple_body() {
	atf_require python3 "Requires python3 to run this test"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgA" "pkgA" "1.0" "/"
	touch a
	echo ${TMPDIR}/a > plist
	atf_check pkg create -M pkgA.ucl -p plist
	atf_check -o ignore pkg add pkgA-1.0.pkg

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgB" "pkgB" "2.0" "/"
	touch b
	echo ${TMPDIR}/b > plist
	atf_check pkg create -M pkgB.ucl -p plist
	atf_check -o ignore pkg add pkgB-2.0.pkg

	# json-compact with multiple packages must also be valid JSON
	atf_check -o save:out.json pkg info --raw --raw-format json-compact pkgA pkgB
	atf_check -o ignore -e empty python3 -m json.tool out.json
	atf_check -o inline:"2\n" python3 -c "import json; print(len(json.load(open('out.json'))))"
}

raw_json_all_body() {
	atf_require python3 "Requires python3 to run this test"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgA" "pkgA" "1.0" "/"
	touch a
	echo ${TMPDIR}/a > plist
	atf_check pkg create -M pkgA.ucl -p plist
	atf_check -o ignore pkg add pkgA-1.0.pkg

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkgB" "pkgB" "2.0" "/"
	touch b
	echo ${TMPDIR}/b > plist
	atf_check pkg create -M pkgB.ucl -p plist
	atf_check -o ignore pkg add pkgB-2.0.pkg

	# -a with raw JSON must be a valid JSON array
	atf_check -o save:out.json pkg info -a --raw --raw-format json
	atf_check -o ignore -e empty python3 -m json.tool out.json
	atf_check -o inline:"2\n" python3 -c "import json; print(len(json.load(open('out.json'))))"
}
