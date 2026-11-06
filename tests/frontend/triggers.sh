#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	cleanup_lua \
	deferred \
	deferred_malformed \
	glob_trigger \
	regexp_trigger \
	path_trigger \
	pkg_exec_sandbox \
	pkg_exec_no_sandbox \
	pkg_add \
	perpackage_pre_install \
	perpackage_post_install \
	perpackage_pre_deinstall \
	perpackage_post_deinstall \
	perpackage_file_glob \
	perpackage_file_regexp

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

deferred_malformed_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_glob: [ "/*" ]
trigger: {
	type: lua
	sandbox: false
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
	atf_check pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" \
	    -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	# a deferred trigger which never ends its headers is dropped
	cat << EOF > ${TMPDIR}/target/var/db/pkg/triggers/broken
--pkgname test
print("never run")
EOF
	atf_check \
		-o ignore \
		-e match:"Ignoring the malformed deferred trigger 'broken'" \
		-s exit:0 \
		pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
	atf_check ls ${TMPDIR}/target/var/db/pkg/triggers
}

deferred_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_glob: [ "/*" ]
trigger: {
	type: lua
	sandbox: false
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
OUTPUT='--begin args
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

regexp_trigger_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_regexp: [ ".*trigger.*" ]
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

pkg_add_body() {
	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	mkdir trigger_dir/
	cat << EOF >> trigger_dir/trigger.ucl
path_glob: [ "/*" ]
trigger: {
	type: lua
	sandbox: false
	script: <<EOS
print("plop")
EOS
}
EOF
	echo "${TMPDIR}"/trigger_dir/trigger.ucl > plist
	atf_check pkg create -M test.ucl -p plist

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "meh" "meh" "1" "/"
	cat << EOF >> meh.ucl
deps: { test: { version: "1", origin: "dontcare" } }
EOF
	echo "@dir ${TMPDIR}"/trigger_dir/ > plist
	mkdir target
	unset PKG_TRIGGERS_DIR
	atf_check pkg create -M meh.ucl -p plist
OUTPUT="Installing meh-1...
\`-- Installing test-1...
\`-- Extracting test-1:  done
Extracting meh-1:  done
==> Running trigger: trigger.ucl
plop
"
	atf_check -o inline:"${OUTPUT}" pkg -o PKG_TRIGGERS_DIR="${TMPDIR}/trigger_dir" add meh-1.pkg
}

perpackage_pre_install_body() {
	# Create a trigger in the pre_install/ subdirectory
	mkdir -p target/trigger_dir/pre_install/
	cat << EOF >> target/trigger_dir/pre_install/ldconfig.ucl
path_regexp: [ ".*/ldconfig" ]
trigger: {
	type: lua
	sandbox: false
	script: <<EOS
print("ldconfig: pre_install for " .. pkg_name .. "-" .. pkg_version)
print("upgrade: " .. tostring(pkg_upgrade))
for i, v in ipairs(arg) do
	print("  path: " .. v)
end
EOS
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "mylib" "mylib" "1.0" "/usr/local"
	mkdir -p usr/local/libdata/ldconfig/
	echo "# ldconfig hints" > usr/local/libdata/ldconfig/mylib.conf
	echo /usr/local/libdata/ldconfig/mylib.conf > plist
	atf_check pkg create -M mylib.ucl -p plist -r .
	unset PKG_TRIGGERS_DIR
	unset PKG_DBDIR
	# A non sandboxed trigger from the target root must not run on the
	# host: it is deferred with its package context
	atf_check -o not-match:"ldconfig: pre_install" \
	    pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target install -qfy ${TMPDIR}/mylib-1.0.pkg
	atf_check -o match:"^--pkgname mylib$" \
	    -o match:"^--pkgversion 1.0$" \
	    -o not-match:"^--upgrade$" \
	    -o match:"^-- /usr/local/libdata/ldconfig$" \
	    cat ${TMPDIR}/target/var/db/pkg/triggers/*
	atf_check -o match:"ldconfig: pre_install for mylib-1.0" \
	    -o match:"upgrade: false" \
	    -o match:"path: /usr/local/libdata/ldconfig" \
	    pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
	atf_check ls ${TMPDIR}/target/var/db/pkg/triggers
}

perpackage_post_install_body() {
	# Create a trigger in the post_install/ subdirectory
	mkdir -p target/trigger_dir/post_install/
	cat << EOF >> target/trigger_dir/post_install/ldconfig.ucl
path_regexp: [ ".*/ldconfig" ]
trigger: {
	type: lua
	sandbox: false
	script: <<EOS
print("ldconfig: post_install for " .. pkg_name .. "-" .. pkg_version)
print("upgrade: " .. tostring(pkg_upgrade))
for i, v in ipairs(arg) do
	print("  path: " .. v)
end
EOS
}
EOF
	# Create a package that installs a file into /usr/local/libdata/ldconfig/
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "mylib" "mylib" "1.0" "/usr/local"
	mkdir -p usr/local/libdata/ldconfig/
	echo "# ldconfig hints" > usr/local/libdata/ldconfig/mylib.conf
	echo /usr/local/libdata/ldconfig/mylib.conf > plist
	atf_check pkg create -M mylib.ucl -p plist -r .
	unset PKG_TRIGGERS_DIR
	unset PKG_DBDIR
	atf_check -o not-match:"ldconfig: post_install" \
	    pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target install -qfy ${TMPDIR}/mylib-1.0.pkg
	atf_check -o match:"ldconfig: post_install for mylib-1.0" \
	    -o match:"upgrade: false" \
	    -o match:"path: /usr/local/libdata/ldconfig" \
	    pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
	# the same trigger is reported as an upgrade when reinstalling
	atf_check -o not-match:"ldconfig: post_install" \
	    pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target install -qfy ${TMPDIR}/mylib-1.0.pkg
	atf_check -o match:"ldconfig: post_install for mylib-1.0" \
	    -o match:"upgrade: true" \
	    pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
}

perpackage_pre_deinstall_body() {
	# Install a package first, then delete it with a pre_deinstall trigger
	mkdir -p target/trigger_dir/pre_deinstall/
	cat << EOF >> target/trigger_dir/pre_deinstall/ldconfig.ucl
path_regexp: [ ".*/ldconfig" ]
trigger: {
	type: lua
	sandbox: false
	script: <<EOS
print("ldconfig: pre_deinstall for " .. pkg_name .. "-" .. pkg_version)
print("upgrade: " .. tostring(pkg_upgrade))
for i, v in ipairs(arg) do
	print("  path: " .. v)
end
EOS
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "mylib" "mylib" "1.0" "/usr/local"
	mkdir -p usr/local/libdata/ldconfig/
	echo "# ldconfig hints" > usr/local/libdata/ldconfig/mylib.conf
	echo /usr/local/libdata/ldconfig/mylib.conf > plist
	atf_check pkg create -M mylib.ucl -p plist -r .
	unset PKG_TRIGGERS_DIR
	unset PKG_DBDIR
	atf_check pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target install -qfy ${TMPDIR}/mylib-1.0.pkg
	atf_check -o not-match:"ldconfig: pre_deinstall" \
	    pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target delete -qy mylib
	atf_check -o match:"ldconfig: pre_deinstall for mylib-1.0" \
	    -o match:"upgrade: false" \
	    -o match:"path: /usr/local/libdata/ldconfig" \
	    pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
}

perpackage_post_deinstall_body() {
	# Install a package first, then delete it with a post_deinstall trigger
	mkdir -p target/trigger_dir/post_deinstall/
	cat << EOF >> target/trigger_dir/post_deinstall/ldconfig.ucl
path_regexp: [ ".*/ldconfig" ]
trigger: {
	type: lua
	sandbox: false
	script: <<EOS
print("ldconfig: post_deinstall for " .. pkg_name .. "-" .. pkg_version)
print("upgrade: " .. tostring(pkg_upgrade))
for i, v in ipairs(arg) do
	print("  path: " .. v)
end
EOS
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "mylib" "mylib" "1.0" "/usr/local"
	mkdir -p usr/local/libdata/ldconfig/
	echo "# ldconfig hints" > usr/local/libdata/ldconfig/mylib.conf
	echo /usr/local/libdata/ldconfig/mylib.conf > plist
	atf_check pkg create -M mylib.ucl -p plist -r .
	unset PKG_TRIGGERS_DIR
	unset PKG_DBDIR
	atf_check pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target install -qfy ${TMPDIR}/mylib-1.0.pkg
	atf_check -o not-match:"ldconfig: post_deinstall" \
	    pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target delete -qy mylib
	atf_check -o match:"ldconfig: post_deinstall for mylib-1.0" \
	    -o match:"upgrade: false" \
	    -o match:"path: /usr/local/libdata/ldconfig" \
	    pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
}

perpackage_file_glob_body() {
	# Verify per-package triggers can match individual files, not just directories
	mkdir -p target/trigger_dir/post_install/
	cat << EOF >> target/trigger_dir/post_install/omf.ucl
path_glob: [ "*/share/omf/*/*.omf" ]
trigger: {
	type: lua
	sandbox: false
	script: <<EOS
for i, v in ipairs(arg) do
	print("omf: " .. v)
end
EOS
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "myhelp" "myhelp" "1.0" "/usr/local"
	mkdir -p usr/local/share/omf/myhelp/
	echo "<omf/>" > usr/local/share/omf/myhelp/myhelp-C.omf
	echo /usr/local/share/omf/myhelp/myhelp-C.omf > plist
	atf_check pkg create -M myhelp.ucl -p plist -r .
	unset PKG_TRIGGERS_DIR
	unset PKG_DBDIR
	atf_check -o not-match:"omf:" \
	    pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target install -qfy ${TMPDIR}/myhelp-1.0.pkg
	atf_check -o match:"omf: /usr/local/share/omf/myhelp/myhelp-C.omf" \
	    pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
}

perpackage_file_regexp_body() {
	# Verify per-package triggers can match individual files via regexp
	mkdir -p target/trigger_dir/post_install/
	cat << EOF >> target/trigger_dir/post_install/schemas.ucl
path_regexp: [ ".*/etc/gconf/schemas/.*\\.schemas$" ]
trigger: {
	type: lua
	sandbox: false
	script: <<EOS
for i, v in ipairs(arg) do
	print("schema: " .. v)
end
EOS
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "myapp" "myapp" "2.0" "/usr/local"
	mkdir -p usr/local/etc/gconf/schemas/
	echo "<schema/>" > usr/local/etc/gconf/schemas/myapp.schemas
	echo /usr/local/etc/gconf/schemas/myapp.schemas > plist
	atf_check pkg create -M myapp.ucl -p plist -r .
	unset PKG_TRIGGERS_DIR
	unset PKG_DBDIR
	atf_check -o not-match:"schema:" \
	    pkg -o REPOS_DIR=/dev/null -o PKG_TRIGGERS_DIR="/trigger_dir" -r ${TMPDIR}/target install -qfy ${TMPDIR}/myapp-2.0.pkg
	atf_check -o match:"schema: /usr/local/etc/gconf/schemas/myapp.schemas" \
	    pkg -o PKG_DBDIR=${TMPDIR}/target/var/db/pkg triggers
}
