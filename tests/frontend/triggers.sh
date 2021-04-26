#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	cleanup_shell \
	cleanup_lua

cleanup_shell_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path: [ "/" ]
cleanup: {
	type: shell
	script: <<EOS
echo "Cleaning up"
EOS
}
trigger: {
	type: shell
	script: <<EOS
exit 0
EOS
}
EOF
	echo ${TMPDIR}/trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist
	mkdir target
	atf_check pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
	atf_check pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" install -qfy ${TMPDIR}/test-1.pkg
	atf_check -o inline:"Cleaning up\n" pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" delete -qy test
}

cleanup_lua_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path: [ "/" ]
cleanup: {
	type: lua
	script: <<EOS
print "Cleaning up"
EOS
}
trigger: {
	type: lua
	script: <<EOS
	return 0
EOS
}
EOF
	echo ${TMPDIR}/trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist
	mkdir target
	atf_check pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
	atf_check pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" install -qfy ${TMPDIR}/test-1.pkg
	atf_check -o inline:"Cleaning up\n" pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" delete -qy test
}
