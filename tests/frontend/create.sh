#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	create_from_plist \
	create_from_plist_set_owner \
	create_from_plist_set_group \
	create_from_plist_gather_mode \
	create_from_plist_set_mode \
	create_from_plist_mini \
	create_from_plist_dirrm \
	create_from_plist_ignore \
	create_from_plist_fflags create_from_plist_bad_fflags \
	create_from_plist_with_keyword_arguments \
	create_from_manifest_and_plist \
	create_from_manifest \
	create_from_plist_pkg_descr \
	create_from_plist_hash \
	create_from_plist_with_keyword_and_message \
	create_from_plist_include \
	create_with_hardlink \
	create_no_clobber \
	time \
	create_from_plist_keyword_validation \
	create_from_plist_keyword_real_args \
	create_from_plist_keyword_lua_actions \
	create_from_plist_keyword_deprecated

genmanifest() {
	cat << EOF >> +MANIFEST
name: test
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /
abi = "*";
desc: <<EOD
Yet another test
EOD
EOF
}

genplist() {
	cat << EOF >> test.plist
$@
EOF
}

preparetestcredentials() {
	touch file1

	genmanifest
	genplist "@$1 file1"
}

basic_validation() {
	test -f test-1.txz || atf_fail "Package not created"
	xz -t test-1.txz || atf_fail "XZ integrity check failed"
}

create_with_hardlink_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1.0"
	echo "blah" >> foo
	ln foo bar
	echo "@(root,wheel,0555,) /foo" >> test.plist
	echo "@config(root,wheel,0644,) /bar" >> test.plist

	atf_check \
		-o ignore \
		-e ignore \
		pkg create -M test.ucl -p test.plist -r .
}

create_from_plist_body() {
	touch file1
	genmanifest
	genplist "file1"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .

	basic_validation
	atf_check \
		-o match:"-rw-r--r-- .*root[ /]+wheel.* /file1$" \
		-e ignore \
		-s exit:0 \
		tar tvf test-1.txz
}

create_from_plist_set_owner_body() {

	preparetestcredentials "(plop,,)"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .

	basic_validation
	atf_check \
		-o match:"-rw-r--r-- .*plop[ /]+wheel.* /file1$" \
		-e ignore \
		-s exit:0 \
		tar tvf test-1.txz
}

create_from_plist_set_group_body() {

	preparetestcredentials "(,bla,)"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .

	basic_validation
	atf_check \
		-o match:"-rw-r--r-- .*root[ /]+bla.* /file1$" \
		-e ignore \
		-s exit:0 \
		tar tvf test-1.txz
}

create_from_plist_gather_mode_body() {

	preparetestcredentials "(plop,bla,)"

	chmod 777 file1 || atf_fail "Impossible to change mode"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .

	basic_validation
	atf_check \
		-o match:"-rwxrwxrwx .*plop[ /]+bla.* /file1$" \
		-e ignore \
		-s exit:0 \
		tar tvf test-1.txz
}

create_from_plist_set_mode_body() {

	preparetestcredentials "(,,2755)"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .

	basic_validation
	atf_check \
		-o match:"-rwxr-sr-x .*root[ /]+wheel.* /file1$" \
		-e ignore \
		-s exit:0 \
		tar tvf test-1.txz
}

create_from_plist_mini_body() {

	preparetestcredentials "(plop,)"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .

	basic_validation
	atf_check \
		-o match:"-rw-r--r-- .*plop[ /]+wheel.* /file1$" \
		-e ignore \
		-s exit:0 \
		tar tvf test-1.txz
}

create_from_plist_dirrm_body() {
	mkdir testdir

	genmanifest
	for dir in dirrm dirrmtry ; do
		rm test.plist
		genplist "@${dir} testdir"

		atf_check \
			-o empty \
			-e inline:"${PROGNAME}: Warning: @dirrm[try] is deprecated, please use @dir\n" \
			pkg create -o ${TMPDIR} -m . -p test.plist -r .

		basic_validation

	done
}

create_from_plist_ignore_body() {
	genmanifest
	genplist "@ignore
aline"
	atf_check \
		-o empty \
		-e empty \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .

	basic_validation

	atf_check \
		-o inline:"+COMPACT_MANIFEST\n+MANIFEST\n" \
		-e empty \
		-s exit:0 \
		tar tf test-1.txz

	atf_check \
		-o empty \
		-e inline:"${PROGNAME}: Warning: @ignore is deprecated\n" \
		pkg -o DEVELOPER_MODE=yes create -o ${TMPDIR} -m . -p test.plist -r .
}

create_from_plist_fflags_body() {
	atf_skip_on Linux does not support fflags
	preparetestcredentials "(,,,schg)"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .
}

create_from_plist_bad_fflags_body() {
	atf_skip_on Linux does not support fflags
	preparetestcredentials "(,,,schg,bad)"

	atf_check \
		-o empty \
		-e inline:"${PROGNAME}: Malformed keyword '', wrong fflags\n" \
		-s exit:1 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .
}

create_from_plist_keyword_real_args_body() {
	preparetestcredentials "test"

cat << EOF > test.ucl
actions: []
arguments: true
post-install-lua: <<EOS
if arg ~= nil then
	print("yes")
end
for i = 1, #arg do
	print(arg[i])
end
EOS
EOF

	genplist "@test A B C D"

mkdir target

	atf_check \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

	atf_check \
		-o inline:"yes\nfile1\nyes\nA\nB\nC\nD\n" \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz
}

create_from_plist_keyword_validation_body() {
	preparetestcredentials "test"

cat << EOF > test.ucl
actions: []
prepackaging: <<EOS
io.stderr:write("meh\n")
return 1
EOS
EOF
	atf_check \
		-o empty \
		-e inline:"meh\n${PROGNAME}: Fail to apply keyword 'test'\n" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF > test.ucl
actions: []
prepackaging: <<EOS
print(line)
io.stderr:write("meh\n")
return 1
EOS
EOF
	atf_check \
		-o inline:"file1\n" \
		-e inline:"meh\n${PROGNAME}: Fail to apply keyword 'test'\n" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF > test.ucl
actions: []
prepackaging: <<EOS
print(#arg)
io.stderr:write("meh\n")
return 1
EOS
EOF
	atf_check \
		-o inline:"0\n" \
		-e inline:"meh\n${PROGNAME}: Fail to apply keyword 'test'\n" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF > test.ucl
actions: []
arguments: true
prepackaging: <<EOS
print(#arg)
io.stderr:write("meh\n")
return 1
EOS
EOF
	atf_check \
		-o inline:"1\n" \
		-e inline:"meh\n${PROGNAME}: Fail to apply keyword 'test'\n" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

	genplist "@test A B"
	genplist "@test A A"
	genplist "@test A B C"
cat << EOF > test.ucl
actions: []
arguments: true
prepackaging: <<EOS
if #arg == 1 then
  return 0
end
if #arg == 2 then
  if arg[1] == arg[2] then
    io.stderr:write("The first and the second argument are identical\n")
    return 1
  end
  return 1
end
io.stderr:write("Invalid number of arguments '".. #arg .. "' expecting 1 or 2\n")
return 1
EOS
EOF
output="${PROGNAME}: Fail to apply keyword 'test'
The first and the second argument are identical
${PROGNAME}: Fail to apply keyword 'test'
Invalid number of arguments '3' expecting 1 or 2
${PROGNAME}: Fail to apply keyword 'test'
"
	atf_check \
		-e inline:"${output}" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .
}

create_from_plist_with_keyword_arguments_body() {
	preparetestcredentials "testkeyword"

	atf_check \
		-o empty \
		-e inline:"${PROGNAME}: cannot load keyword from ./testkeyword.ucl: No such file or directory\n${PROGNAME}: unknown keyword testkeyword: @testkeyword\n" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF >> testkeyword.ucl
actions: []
arguments: true
post-install:
	echo %1 %2
EOF

	atf_check \
		-o empty \
		-e inline:"${PROGNAME}: Requesting argument %2 while only 1 arguments are available\n${PROGNAME}: Fail to apply keyword 'testkeyword'\n" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF > testkeyword.ucl
actions: [file(%1)]
arguments: true
post-install:
	echo %1 %2
EOF

	echo "@testkeyword A B" > test.plist

	atf_check \
		-o empty \
		-e inline:"${PROGNAME}: Invalid argument: expecting a number got (%1)\n${PROGNAME}: Fail to apply keyword 'testkeyword'\n" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF > testkeyword.ucl
actions: [file(1), dir(2)]
arguments: true
post-install:
	echo %1 %2
EOF
	touch A
	mkdir B

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF >> output.ucl
name = "test";
origin = "test";
version = "1";
comment = "a test";
maintainer = "test";
www = "http://test";
abi = "*";
arch = "*";
prefix = "/";
flatsize = 0;
desc = "Yet another test";
categories [
    "test",
]
files {
    /A = "1\$e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
}
directories {
    /B = "y";
}
scripts {
    post-install = "# args: A B\necho A B";
}

EOF

	atf_check \
		-o file:output.ucl \
		-e empty \
		-s exit:0 \
		pkg info -R --raw-format=ucl -F test-1.txz
}

create_from_manifest_and_plist_body() {
	genmanifest
	touch testfile
	genplist "testfile"
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./+MANIFEST -p test.plist -r ${TMPDIR}

	cat << EOF > output.ucl
name = "test";
origin = "test";
version = "1";
comment = "a test";
maintainer = "test";
www = "http://test";
abi = "*";
arch = "*";
prefix = "/";
flatsize = 0;
desc = "Yet another test";
categories [
    "test",
]
files {
    /testfile = "1\$e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
}

EOF

	atf_check \
		-o file:output.ucl \
		-e empty \
		-s exit:0 \
		pkg info -R --raw-format=ucl -F test-1.txz
}

create_from_manifest_body() {
	genmanifest
	cat <<EOF >> +MANIFEST
files: {
     /testfile: {perm: 0644}
}
EOF
	touch testfile
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./+MANIFEST -r ${TMPDIR}

	cat << EOF > output.ucl
name = "test";
origin = "test";
version = "1";
comment = "a test";
maintainer = "test";
www = "http://test";
abi = "*";
arch = "*";
prefix = "/";
flatsize = 0;
desc = "Yet another test";
categories [
    "test",
]
files {
    /testfile = "1\$e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
}

EOF

	atf_check \
		-o file:output.ucl \
		-e empty \
		-s exit:0 \
		pkg info -R --raw-format=ucl -F test-1.txz
}

create_from_plist_pkg_descr_body() {
	genmanifest
cat << EOF > ./+DISPLAY
Message
EOF

OUTPUT="test-1:
Always:
Message

"
	atf_check pkg create -m . -r ${TMPDIR}
	atf_check -o inline:"${OUTPUT}" pkg info -D -F ./test-1.txz

cat << EOF > ./+DISPLAY
[
	{ message: "message" },
	{ message: "message upgrade", type = "upgrade" },
]
EOF

OUTPUT='test-1:
Always:
message

On upgrade:
message upgrade

'

	atf_check pkg create -m . -r ${TMPDIR}
	atf_check -o inline:"${OUTPUT}" pkg info -D -F ./test-1.txz

}

create_from_plist_hash_body() {
	touch file1
	genmanifest
	genplist "file1"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -h -o ${TMPDIR} -m . -p test.plist -r .

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		ls test-1-*.txz
}

create_from_plist_with_keyword_and_message_body() {
	genmanifest
	genplist "@showmsg plop"
cat << EOF > showmsg.ucl
actions: []
messages: [
	{ message: "always" },
	{ message: "on upgrade";type = "upgrade" },
	{ message: "on install"; type = "install" },
]
EOF
cat << EOF > +DISPLAY
old message
EOF

OUTPUT='test-1:
Always:
old message

Always:
always

On upgrade:
on upgrade

On install:
on install

'
	atf_check pkg -o PLIST_KEYWORDS_DIR=. create -m . -r ${TMPDIR} -p test.plist
	atf_check -o inline:"${OUTPUT}" pkg info -D -F ./test-1.txz

}

time_body() {
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
files: {
	"${TMPDIR}/a" = "";
}
EOF
	touch a
	pattern=$(ls -l ${TMPDIR}/a | awk '{print $6" +"$7" +"$8".*/a"}')
	atf_check pkg create -M test.ucl
	atf_check env SOURCE_DATE_EPOCH=86400 pkg create -M test.ucl
	atf_check \
		-o match:"0 Jan +2 +1970.*/a" \
		tar tvf test-1.txz
	atf_check -e match:"Invalid" -s exit:1 pkg create -t meh -M test.ucl
	atf_check pkg create -t 172800 -M test.ucl
	atf_check \
		-o match:"0 Jan +3 +1970.*/a" \
		tar tvf test-1.txz
	atf_check env SOURCE_DATE_EPOCH=86400 pkg create -t 172800 -M test.ucl
	atf_check \
		-o match:"0 Jan +3 +1970.*/a" \
		tar tvf test-1.txz
	atf_check pkg create -M test.ucl
	atf_check \
		-o match:"${pattern}" \
		tar tvf test-1.txz

	mkdir target
	atf_check -o empty \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz
	atf_check \
		-o match:"${pattern}" \
		ls -l ${TMPDIR}/target/${TMPDIR}/a
}

create_no_clobber_body()
{
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	touch test-1.txz
	before=$(ls -l test-1.txz)
	atf_check pkg create -nM test.ucl
	after=$(ls -l test-1.txz)
	[ "$before" = "$after" ] || atf_fail "Package was recreated"
}

create_from_plist_include_body()
{
	genmanifest
	cat << EOF >> test.plist
file1
@include other-plist
file2
EOF
	cat <<EOF >> other-plist
file3
EOF

	touch file1
	touch file2
	touch file3

	atf_check \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .

	atf_check -o inline:"/file1\n/file3\n/file2\n" pkg info -ql -F test*.txz
	cat << EOF >> other-plist
@include test.plist
EOF
	atf_check \
		-e inline:"pkg: Inside in @include it is not allowed to reuse @include\n" \
		-s exit:1 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .
}

create_from_plist_keyword_lua_actions_body()
{
	genmanifest
	genplist "@test(plop,,) A B C D"

cat << EOF > test.ucl
arguments: true
prepackaging: <<EOS
ok = true
for i = 1, #arg do
	if not pkg.file(arg[i]) then
		ok = false
	end
end
if not ok then
	return 1
end
EOS
arguments: true
EOF

touch C
touch D

output="${PROGNAME}: Unable to access file ./A:No such file or directory
${PROGNAME}: Unable to access file ./B:No such file or directory
${PROGNAME}: Fail to apply keyword 'test'
"

	atf_check \
		-e inline:"${output}" \
		-s exit:1 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

touch A B
	atf_check \
		-s exit:0 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

	atf_check \
		-o match:"-rw-r--r-- .*plop[ /]+wheel.* /A$" \
		tar tvf test-1.txz
}

create_from_plist_keyword_deprecated_body()
{
	genmanifest
	genplist "@test A B C D"

cat << EOF > test.ucl
arguments: true
deprecated: true
EOF

	atf_check \
		-e inline:"${PROGNAME}: Use of '@test' is deprecated\n" \
		-s exit:0 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF > test.ucl
arguments: true
deprecated: true
deprecation_message: <<EOM
we don't like it anymore
EOM
EOF

	atf_check \
		-e inline:"${PROGNAME}: Use of '@test' is deprecated: we don't like it anymore\n" \
		-s exit:0 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

}
