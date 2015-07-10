#! /usr/bin/env atf-sh

atf_test_case create_from_plist
create_from_plist_head() {
	atf_set "descr" "Testing pkg create from plist"
}

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
		-o match:"-rw-r--r-- .*root +wheel.* /file1$" \
		-e empty \
		-s exit:0 \
		tar tvf test-1.txz
}

atf_test_case create_from_plist_set_owner
create_from_plist_set_owner_head() {
	atf_set "descr" "Testing credentials set from the plist"
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
		-o match:"-rw-r--r-- .*plop +wheel.* /file1$" \
		-e empty \
		-s exit:0 \
		tar tvf test-1.txz
}

atf_test_case create_from_plist_set_group
create_from_plist_set_owner_head() {
	atf_set "descr" "Testing credentials set from the plist"
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
		-o match:"-rw-r--r-- .*root +bla.* /file1$" \
		-e empty \
		-s exit:0 \
		tar tvf test-1.txz
}

atf_test_case create_from_plist_gather_mode
create_from_plist_gather_mode_head() {
	atf_set "descr" "Testing credentials set from the plist"
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
		-o match:"-rwxrwxrwx .*plop +bla.* /file1$" \
		-e empty \
		-s exit:0 \
		tar tvf test-1.txz
}

atf_test_case create_from_plist_set_mode
create_from_plist_set_mode_head() {
	atf_set "descr" "Testing credentials set from the plist"
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
		-o match:"-rwxr-sr-x .*root +wheel.* /file1$" \
		-e empty \
		-s exit:0 \
		tar tvf test-1.txz
}

atf_test_case create_from_plist_mini
create_from_plist_mini_head() {
	atf_set "descr" "Testing credentials set from the plist"
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
		-o match:"-rw-r--r-- .*plop +wheel.* /file1$" \
		-e empty \
		-s exit:0 \
		tar tvf test-1.txz
}

atf_test_case create_from_plist_dirrm
create_from_plist_dirrm_head() {
	atf_set "descr" "Testing @dirrm(try) set from the plist"
}

create_from_plist_dirrm_body() {
	mkdir testdir

	genmanifest
	for dir in dirrm dirrmtry ; do
		rm test.plist
		genplist "@${dir} testdir"

		atf_check \
			-o empty \
			-e empty \
			pkg create -o ${TMPDIR} -m . -p test.plist -r .

		basic_validation

		atf_check \
			-o empty \
			-e inline:"pkg: Warning: @dirrm[try] is deprecated, please use @dir\n" \
			pkg -o DEVELOPER_MODE=yes create -o ${TMPDIR} -m . -p test.plist -r .
	done
}

atf_test_case create_from_plist_ignore
create_from_plist_ignore_head() {
	atf_set "descr" "Testing @ignore"
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
		-e inline:"pkg: Warning: @ignore is deprecated\n" \
		pkg -o DEVELOPER_MODE=yes create -o ${TMPDIR} -m . -p test.plist -r .
}

atf_test_case create_from_plist_fflags
create_from_plist_fflags_head() {
	atf_set "descr" "Test fflags set from plist"
}

create_from_plist_fflags_body() {
	preparetestcredentials "(,,,schg)"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .
}

atf_test_case create_from_plist_bad_fflags
create_from_plist_bad_fflags_head() {
	atf_set "descr" "Test bad fflags set from plist"
}

create_from_plist_bad_fflags_body() {
	preparetestcredentials "(,,,schg,bad)"

	atf_check \
		-o empty \
		-e inline:"pkg: Malformed keyword '', wrong fflags\n" \
		-s exit:70 \
		pkg create -o ${TMPDIR} -m . -p test.plist -r .
}

atf_test_case create_from_plist_with_keyword_arguments
create_from_plist_with_keyword_arguments_head() {
	atf_set "descr" "Test keywords with arguments"
}

create_from_plist_with_keyword_arguments_body() {
	preparetestcredentials "testkeyword"

	atf_check \
		-o empty \
		-e inline:"pkg: cannot parse keyword: cannot open file ./testkeyword.ucl: No such file or directory\npkg: unknown keyword testkeyword: @testkeyword\n" \
		-s exit:70 \
		pkg -o PLIST_KEYWORDS_DIR=. create -o ${TMPDIR} -m . -p test.plist -r .

cat << EOF >> testkeyword.ucl
actions: []
arguments: true
post-install:
	echo %1 %2
EOF

	atf_check \
		-o empty \
		-e inline:"pkg: Requesting argument %2 while only 1 arguments are available\n" \
		-s exit:70 \
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
		-e inline:"pkg: Invalid argument: expecting a number got (%1)\n" \
		-s exit:70 \
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
    post-install = "echo A B";
}

EOF

	atf_check \
		-o file:output.ucl \
		-e empty \
		-s exit:0 \
		pkg info -R --raw-format=ucl -F test-1.txz
}

atf_test_case create_from_manifest_and_plist
create_from_manifest_and_plist_head() {
	atf_set "descr" "Testing pkg create with manifest and plist"
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

atf_init_test_cases() {
	. $(atf_get_srcdir)/test_environment.sh

	atf_add_test_case create_from_plist
	atf_add_test_case create_from_plist_set_owner
	atf_add_test_case create_from_plist_set_group
	atf_add_test_case create_from_plist_gather_mode
	atf_add_test_case create_from_plist_set_mode
	atf_add_test_case create_from_plist_mini
	atf_add_test_case create_from_plist_dirrm
	atf_add_test_case create_from_plist_ignore
	if [ `uname -s` != "Linux" ] ; then
		atf_add_test_case create_from_plist_fflags
		atf_add_test_case create_from_plist_bad_fflags
	fi
	atf_add_test_case create_from_plist_with_keyword_arguments
	atf_add_test_case create_from_manifest_and_plist
}
