#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	raw_json_single \
	raw_json_multiple \
	raw_json_compact_multiple \
	raw_json_all

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
