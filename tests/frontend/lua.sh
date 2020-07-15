#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	script_basic \
	script_message \
	script_rooteddir \
	script_remove \
	script_execute \
	script_rename \
	script_upgrade \
	script_sample_not_exists \
	script_sample_exists

script_basic_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [
  'print("this is post install1")',
  'print("this is post install2")',
  ]
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o inline:"this is post install1\nthis is post install2\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

}

script_message_body() {
	# The message should be the last thing planned
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [
  "print(\"this is post install1\")\npkg.print_msg(\"this is a message\")",
  'print("this is post install2")',
  ]
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o inline:"this is post install1\nthis is post install2\nthis is a message\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

}

script_rooteddir_body() {
	# The message should be the last thing planned
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [ <<EOS
test = io.open("/file.txt", "w+")
test:write("test\n")
io.close(test)
EOS
,
  ]
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz
	[ -f ${TMPDIR}/target/file.txt ] || atf_fail "File not created in the rootdir"
	# test the mode
	atf_check -o match:"^-rw-r--r--" ls -l ${TMPDIR}/target/file.txt
	atf_check \
		-e empty \
		-o inline:"test\n" \
		-s exit:0 \
		cat ${TMPDIR}/target/file.txt

}

script_remove_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [ <<EOS
os.remove("/file")
EOS
,
  ]
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir -p ${TMPDIR}/target/file
	atf_check \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz
	test -d ${TMPDIR}/target/file && atf_fail "directory not removed"

	touch ${TMPDIR}/target/file
	atf_check \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz
	test -f ${TMPDIR}/target/file && atf_fail "file not removed"
	return 0
}

script_rename_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [ <<EOS
os.rename("/file","/plop")
EOS
,
  ]
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl
	mkdir -p ${TMPDIR}/target
	touch ${TMPDIR}/target/file
	atf_check \
		-e inline:"${ERR}" \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz
	test -f ${TMPDIR}/target/file && atf_fail "File not renamed"
	test -f ${TMPDIR}/target/plop || atf_fail "File not renamed"
	return 0
}

script_execute_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [ <<EOS
os.execute("echo yeah")
EOS
,
  ]
}
EOF

ERR="pkg: Failed to execute lua script: [string \"os.execute(\"echo yeah\")\"]:1: os.execute not available
pkg: lua script failed\n"


	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl
	mkdir -p ${TMPDIR}/target
	atf_check \
		-e inline:"${ERR}" \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz
}

script_upgrade_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [ <<EOS
if pkg_upgrade then
 pkg.print_msg("upgrade : ".. tostring(pkg_upgrade))
end
EOS
,
  ]
}
EOF


	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl
	mkdir -p ${TMPDIR}/target
	atf_check \
		-e empty \
		-o empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [ <<EOS
if pkg_upgrade then
 pkg.print_msg("upgrade:".. tostring(pkg_upgrade))
end
EOS
,
  ]
}
EOF

	rm ${TMPDIR}/test-1.txz
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl
	mkdir -p ${TMPDIR}/target
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo .
	mkdir reposconf
	cat <<EOF >> reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF
	atf_check \
		-e empty \
		-o match:"upgrade:true" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -r ${TMPDIR}/target upgrade -y
}

script_sample_not_exists_body() {
	echo "sample text" > a.sample
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a.sample: ""
}
lua_scripts: {
  post-install: [ <<EOS
  if pkg_filecmp("${TMPDIR}/a.sample", "${TMPDIR}/a") == 2 then
     pkg_copy("${TMPDIR}/a.sample", "${TMPDIR}/a", true)
  end
EOS
, ]
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

	cat ${TMPDIR}/target${TMPDIR}/a.sample
	cat ${TMPDIR}/target${TMPDIR}/a
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		cmp -s ${TMPDIR}/target${TMPDIR}/a.sample ${TMPDIR}/target${TMPDIR}/a
}

script_sample_exists_body() {
	echo "sample text" > a.sample
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a.sample: ""
}
lua_scripts: {
  post-install: [ <<EOS
  if pkg_filecmp("${TMPDIR}/a.sample", "${TMPDIR}/a") == 2 then
     pkg_copy("${TMPDIR}/a.sample", "${TMPDIR}/a", true)
  end
EOS
, ]
}
EOF

	mkdir -p ${TMPDIR}/target${TMPDIR}
	echo "text modified" > ${TMPDIR}/target${TMPDIR}/a
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

	atf_check \
		-o empty \
		-e empty \
		-s exit:1 \
		cmp -s ${TMPDIR}/target${TMPDIR}/a.sample ${TMPDIR}/target${TMPDIR}/a
}
