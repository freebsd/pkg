#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	cleanup_lua \
	deferred \
	glob_trigger \
	regex_trigger \
	path_trigger \
	pkg_exec_sandbox \
	pkg_exec_no_sandbox

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
	unset PKG_TRIGGERS_DIR
	atf_check pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" install -qfy ${TMPDIR}/test-1.pkg
	atf_check -o inline:"Cleaning up\n" pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" delete -qy test
}

deferred_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_glob: [ "/*" ]
trigger: {
	type: lua
	script: <<EOS
	print("deferred " .. arg[1])
EOS
}
EOF
	echo trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist -r .
	mkdir target
	unset PKG_TRIGGERS_DIR
	unset PKG_DBDIR
	atf_check pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
OUTPUT='--sandbox
--begin args
-- /trigger_dir
--end args
--
	print("deferred " .. arg[1])
'
	atf_check -o inline:"${OUTPUT}" cat ${TMPDIR}/target/var/db/pkg/triggers/*
	atf_check -o inline:"deferred /trigger_dir\n" pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
	# test the deferred trigger has been removed
	atf_check ls ${TMPDIR}/target/var/db/pkg/triggers
}

glob_trigger_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_glob: [ "/*" ]
trigger: {
	type: lua
	script: <<EOS
	print("triggered " .. arg[1])
EOS
}
EOF
	echo ${TMPDIR}/trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist
	mkdir target
	unset PKG_TRIGGERS_DIR
	atf_check -o inline:"triggered ${TMPDIR}/trigger_dir\n" pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" install -qfy ${TMPDIR}/test-1.pkg
}

regex_trigger_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_regex: [ ".*trigger.*" ]
trigger: {
	type: lua
	script: <<EOS
	print("triggered " .. arg[1])
EOS
}
EOF
	echo ${TMPDIR}/trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist
	mkdir target
	unset PKG_TRIGGERS_DIR
	atf_check -o inline:"triggered ${TMPDIR}/trigger_dir\n" pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" install -qfy ${TMPDIR}/test-1.pkg
}

path_trigger_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path: [ "${TMPDIR}/trigger_dir" ]
trigger: {
	type: lua
	script: <<EOS
	print("triggered " .. arg[1])
EOS
}
EOF
	echo ${TMPDIR}/trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist
	mkdir target
	unset PKG_TRIGGERS_DIR
	atf_check -o inline:"triggered ${TMPDIR}/trigger_dir\n" pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" install -qfy ${TMPDIR}/test-1.pkg
}

pkg_exec_sandbox_body() {
	atf_skip_on Darwin Requires capsicum
	atf_skip_on Linux Requires capsicum
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_glob: [ "/*" ]
trigger: {
	type: lua
	script: <<EOS
pkg.exec({"echo", "plop"})
EOS
}
EOF
	echo ${TMPDIR}/trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist
	mkdir target
	unset PKG_TRIGGERS_DIR
	atf_check -e inline:"pkg: Failed to execute lua trigger: [string \"pkg.exec({\"echo\", \"plop\"})\"]:1: pkg.exec not available in sandbox\npkg: lua script failed\n" pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" install -qfy ${TMPDIR}/test-1.pkg
}

pkg_exec_no_sandbox_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_glob: [ "/*" ]
trigger: {
	type: lua
	sandbox: false
	script: <<EOS
pkg.exec({"echo", "plop"})
EOS
}
EOF
	echo ${TMPDIR}/trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist
	mkdir target
	unset PKG_TRIGGERS_DIR
	atf_check -o inline:"plop\n" pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" install -qfy ${TMPDIR}/test-1.pkg
}
