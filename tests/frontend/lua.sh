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
	script_filecmp \
	script_filecmp_symlink \
	script_copy \
	script_copy_symlink \
	script_sample_not_exists \
	script_sample_not_exists_symlink \
	script_sample_not_exists_two_files \
	script_sample_exists \
	script_stat \
	script_arguments \
	script_env_isolation \
	script_metalog_add \
	script_exec_capture

script_env_isolation_body() {
	# The variables exported for a script must not leak into the next one
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "one" "one" "1" "/"
	cat << EOF >> one.ucl
scripts: {
  post-install: "echo one"
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "two" "two" "1" "/"
	cat << EOF >> two.ucl
lua_scripts: {
  post-install: [
  'print("leaked=" .. tostring(os.getenv("PKG_NAME")))',
  ]
}
EOF
	atf_check -o ignore -s exit:0 pkg create -M one.ucl
	atf_check -o ignore -s exit:0 pkg create -M two.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o match:"leaked=nil" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target add \
		${TMPDIR}/one-1.pkg ${TMPDIR}/two-1.pkg
}

script_arguments_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [
  "-- args: plop\nprint(arg[1])"
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
		-o inline:"plop\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

}

script_basic_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

}

script_message_body() {
	# The message should be the last thing planned
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
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
		-o save:output \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -fy ${TMPDIR}/test-1.pkg

	# pkg.print_msg output must appear after script stdout
	atf_check -o inline:"this is post install1\nthis is post install2\nthis is a message\n" \
		grep -E "post install|a message" output
}

script_rooteddir_body() {
	# The message should be the last thing planned
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
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
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
	test -d ${TMPDIR}/target/file && atf_fail "directory not removed"

	touch ${TMPDIR}/target/file
	atf_check \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
	test -f ${TMPDIR}/target/file && atf_fail "file not removed"
	return 0
}

script_rename_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
	test -f ${TMPDIR}/target/file && atf_fail "File not renamed"
	test -f ${TMPDIR}/target/plop || atf_fail "File not renamed"
	return 0
}

script_execute_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
}

script_upgrade_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2" "/"
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

	rm ${TMPDIR}/test-1.pkg
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

script_filecmp_body() {
	echo "sametext" > a
	echo "sametext" > b
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a: ""
	${TMPDIR}/b: ""
}
lua_scripts: {
  post-install: [ <<EOS
  if pkg.filecmp("${TMPDIR}/a", "${TMPDIR}/b") == 0 then
     print("same")
  else
     print("different")
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
		-o inline:"same\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	# Cleanup
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target delete -qfy test-1
	rm -rf ${TMPDIR}/target

	echo "sametext" > a
	echo "differenttext" > b
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a: ""
	${TMPDIR}/b: ""
}
lua_scripts: {
  post-install: [ <<EOS
  if pkg.filecmp("${TMPDIR}/a", "${TMPDIR}/b") == 0 then
     print("same")
  else
     print("different")
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
		-o inline:"different\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
}

script_filecmp_symlink_body() {
	echo "sametext" > a
	echo "sametext" > b
	ln -s a c
	ln -s b d
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a: ""
	${TMPDIR}/b: ""
	${TMPDIR}/c: ""
	${TMPDIR}/d: ""
}
lua_scripts: {
  post-install: [ <<EOS
  if pkg.filecmp("${TMPDIR}/c", "${TMPDIR}/d") == 0 then
     print("same")
  else
     print("different")
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
		-o inline:"same\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	# Cleanup
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target delete -qfy test-1
	rm -rf ${TMPDIR}/target
	rm a b c d

	echo "sametext" > a
	echo "differenttext" > b
	ln -s a c
	ln -s b d
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a: ""
	${TMPDIR}/b: ""
	${TMPDIR}/c: ""
	${TMPDIR}/d: ""
}
lua_scripts: {
  post-install: [ <<EOS
  if pkg.filecmp("${TMPDIR}/c", "${TMPDIR}/d") == 0 then
     print("same")
  else
     print("different")
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
		-o inline:"different\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
}

script_copy_body() {
	echo "sample text" > a.sample
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a.sample: ""
}
lua_scripts: {
  post-install: [ <<EOS
   pkg.copy("${TMPDIR}/a.sample", "${TMPDIR}/a")
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/a.sample
	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/a
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		cmp -s ${TMPDIR}/target${TMPDIR}/a.sample ${TMPDIR}/target${TMPDIR}/a
}

script_copy_symlink_body() {
	echo "sample text" > a.sample
	ln -s a.sample b
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a.sample: ""
	${TMPDIR}/b: ""
}
lua_scripts: {
  post-install: [ <<EOS
   pkg.copy("${TMPDIR}/b", "${TMPDIR}/a")
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/a.sample
	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/b
	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/a
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		cmp -s ${TMPDIR}/target${TMPDIR}/a.sample ${TMPDIR}/target${TMPDIR}/a
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		cmp -s ${TMPDIR}/target${TMPDIR}/b ${TMPDIR}/target${TMPDIR}/a
}

script_sample_not_exists_body() {
	echo "sample text" > a.sample
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a.sample: ""
}
lua_scripts: {
  post-install: [ <<EOS
  -- args: a.sample
  sample_file = pkg.prefixed_path(arg[1])
  if arg[2] == nil then
    target_file = string.gsub(sample_file,'%.sample$', "")
  else
    target_file = pkg.prefixed_path(arg[2])
  end
  if not pkg.stat(target_file) then
    pkg.copy(sample_file, target_file)
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/a.sample
	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/a
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		cmp -s ${TMPDIR}/target${TMPDIR}/a.sample ${TMPDIR}/target${TMPDIR}/a
}

script_sample_not_exists_symlink_body() {
	echo "sample text" > a
	ln -s a b.sample
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a: ""
	${TMPDIR}/b.sample: ""
}
lua_scripts: {
  post-install: [ <<EOS
  -- args: b.sample
  sample_file = pkg.prefixed_path(arg[1])
  if arg[2] == nil then
    target_file = string.gsub(sample_file,'%.sample$', "")
  else
    target_file = pkg.prefixed_path(arg[2])
  end
  if not pkg.stat(target_file) then
    pkg.copy(sample_file, target_file)
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/a
	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/b.sample
	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/b
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		cmp -s ${TMPDIR}/target${TMPDIR}/b.sample ${TMPDIR}/target${TMPDIR}/b
}

script_sample_not_exists_two_files_body() {
	echo "sample text" > a
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a: ""
}
lua_scripts: {
  post-install: [ <<EOS
  -- args: a b
  sample_file = pkg.prefixed_path(arg[1])
  if arg[2] == nil then
    target_file = string.gsub(sample_file,'%.sample$', "")
  else
    target_file = pkg.prefixed_path(arg[2])
  end
  if not pkg.stat(target_file) then
    pkg.copy(sample_file, target_file)
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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/a
	atf_check -o inline:"sample text\n" cat ${TMPDIR}/target${TMPDIR}/b
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		cmp -s ${TMPDIR}/target${TMPDIR}/a ${TMPDIR}/target${TMPDIR}/b
}

script_sample_exists_body() {
	echo "sample text" > a.sample
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/a.sample: ""
}
lua_scripts: {
  post-install: [ <<EOS
  -- args: a.sample
  sample_file = pkg.prefixed_path(arg[1])
  if arg[2] == nil then
    target_file = string.gsub(sample_file,'%.sample$', "")
  else
    target_file = pkg.prefixed_path(arg[2])
  end
  if not pkg.stat(target_file) then
    pkg.copy(sample_file, target_file)
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

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	atf_check \
		-o empty \
		-e empty \
		-s exit:1 \
		cmp -s ${TMPDIR}/target${TMPDIR}/a.sample ${TMPDIR}/target${TMPDIR}/a
}

script_stat_body() {
	touch plop
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/plop: ""
}
lua_scripts: {
  post-install: [ <<EOS
  local st = pkg.stat("${TMPDIR}/plop")
  if st.size == 0 then
     print("zero")
  end
  print(st.type)
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
		-o inline:"zero\nreg\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
}

script_metalog_add_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	touch plop
	echo "@(root,wheel,440) /plop" > test.plist
	cat << EOF >> test.ucl
lua_scripts: {
	post-install: [ <<EOS
	assert(pkg.metalog_copy("/plop", "/meh"))
EOS
, ]
}
EOF

OUTPUT="./plop type=file uname=root gname=wheel mode=440
./meh type=file uname=root gname=wheel mode=440
"
	atf_check -o ignore pkg create -M test.ucl -p test.plist -r .
	mkdir ${TMPDIR}/target
	atf_check -s exit:0 \
		pkg \
			-o REPOS_DIR=/dev/null \
			-o METALOG=${TMPDIR}/METALOG \
			-r ${TMPDIR}/target \
			install \
			-qfy ${TMPDIR}/test-1.pkg
	atf_check -o inline:"${OUTPUT}" cat METALOG
}

script_exec_capture_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "/"
	cat << EOF >> test.ucl
lua_scripts: {
  post-install: [ <<EOS
-- Test capturing stdout
local out, err, code = pkg.exec_capture({"/bin/echo", "hello world"})
print("stdout:" .. out)
print("exitcode:" .. tostring(code))

-- Test capturing stderr
local out2, err2, code2 = pkg.exec_capture({"/bin/sh", "-c", "echo errmsg >&2"})
print("stderr:" .. err2)

-- Test exit code
local out3, err3, code3 = pkg.exec_capture({"/bin/sh", "-c", "exit 42"})
print("code:" .. tostring(code3))
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
		-o inline:"stdout:hello world\n\nexitcode:0\nstderr:errmsg\n\ncode:42\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
}
