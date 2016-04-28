#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	basic \
	basic_dirs

basic_body()
{
	echo "test" > a
	new_pkg "test" "test" "1" || atf_fail "plop"
cat << EOF >> test.ucl
files = {
	${TMPDIR}/a: ""
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -r ${TMPDIR}/target install -qy ${TMPDIR}/test-1.txz

OUTPUT="${TMPDIR}/target/local.sqlite
${TMPDIR}/target${TMPDIR}/a
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		-s exit:0 \
		find -s ${TMPDIR}/target -type f -print

	echo "test2" > a
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

# check no leftovers during upgrades/reinstallation
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		-s exit:0 \
		find -s ${TMPDIR}/target -type f -print

}

basic_dirs_body()
{
	mkdir ${TMPDIR}/plop
	new_pkg "test" "test" "1" || atf_fail "plop"
cat << EOF >> test.ucl
directories = {
	${TMPDIR}/plop: y
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

	test -d ${TMPDIR}/target${TMPDIR}/plop || atf_fail "directory not extracted"
}
