#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	reinstall \
	pre_script_fail \
	post_script_ignored

reinstall_body()
{
	new_pkg test test 1 /usr/local

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

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

	cat << EOF > repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}" install -y test
}

pre_script_fail_body()
{
	new_pkg test test 1
	cat << EOF >> test.ucl
scripts: {
   pre-install: "exit 1"
}
EOF

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check -o ignore \
		-e inline:"pkg: PRE-INSTALL script failed\n" \
		-s exit:3 \
		pkg -o REPOS_DIR="/dev/null" install -y ${TMPDIR}/test-1.txz
}

post_script_ignored_body()
{
	new_pkg test test 1
	cat << EOF >> test.ucl
scripts: {
   post-install: "exit 1"
}
EOF

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check -o ignore \
		-e inline:"pkg: POST-INSTALL script failed\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="/dev/null" install -y ${TMPDIR}/test-1.txz
}
