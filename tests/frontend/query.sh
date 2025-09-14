#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	query

query_body() {
	touch plop
	touch bla
	ln -s sym-target sym
	mkdir test-dir
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat >> test.ucl << EOF
options: {
	"OPT1": "on"
	"OPT2": "off"
}
directories: {
      "${TMPDIR}/test-dir": {
		"uname": "uroot"
		"gname": "groot"
		"perm": "660"
		"fflags": "nodump,arch"
      }
}
files: {
	"${TMPDIR}/plop": {
		"sum": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
		"uname": "uroot"
		"gname": "groot"
		"perm": "777"
		"fflags": "schg,nodump"
	}
	"${TMPDIR}/bla": {
		"sum": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
		"uname": "utoor"
		"gname": "gtoor"
		"perm": "1765"
	}
	"${TMPDIR}/sym": {
		"sum": ""
		"uname": "sym-root"
		"gname": "sym-gtoor"
		"perm": "0644"
		"symlink_target": "sym-target"
	}
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test2 test 2
	cat >> test2.ucl << EOF
options: {
	"OPT1": "on"
	"OPT2": "off"
}
files: {
	"${TMPDIR}/plop": ""
	"${TMPDIR}/bla": ""
	"${TMPDIR}/sym": ""
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg plop plop 1
	sed -ie 's/comment: a test/comment: Nothing to see here/' plop.ucl
	cat >> plop.ucl << EOF
deps: {
    test: {
        origin: "test",
        version: "1"
    },

}
EOF

	atf_check \
		-o match:".*Installing.*" \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o match:".*Installing.*" \
		-e empty \
		-s exit:0 \
		pkg register -M plop.ucl

	atf_check \
		-o inline:"plop\ntest\n" \
		-e empty \
		-s exit:0 \
		pkg query "%n"

	atf_check \
		-o inline:"test\n" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#O > 0" "%n"

	atf_check \
		-o inline:"test: plop 1 plop\n" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#r>0" "%n: %rn %rv %ro"

	atf_check \
		-o inline:"test: plop 1 plop\n" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#r>0" "%n: %rn %rv %ro" test

	atf_check \
		-o empty \
		-e empty \
		-s exit:1 \
		pkg query -e "%#r>0" "%n: %rn %rv %ro" plop

	atf_check \
		-o inline:"test: plop 1 plop\n" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#r>0" "%n: %rn %rv %ro" plop test

	atf_check \
		-o inline:"test: plop 1 plop\n" \
		-e empty \
		-s exit:0 \
		pkg query -ge "%#r>0" "%n: %rn %rv %ro" "p*p" "t*t"

	atf_check \
		-o inline:"test: plop 1 plop\n" \
		-e empty \
		-s exit:0 \
		pkg query -xe "%#r>0" "%n: %rn %rv %ro" "p.*p" "t.*t"

	atf_check \
		-o inline:"plop: test 1 test\n" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#d>0" "%n: %dn %dv %do"

	atf_check \
		-o inline:"plop: test 1 test\n" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#d>0" "%n: %dn %dv %do" plop test

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg query -e "%#d>0 && %#r>0" "%n"

	atf_check \
		-o inline:"test\n" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#O > 0 && %#D > 0" "%n"


	atf_check \
	    -o inline:"${TMPDIR}/test-dir\n" \
	    -e empty \
	    -s exit:0 \
	    pkg query "%D" test

	if [ "${OS}" = "FreeBSD" ]; then
	    atf_check \
		-o match:"^${TMPDIR}/bla utoor gtoor 1765 - $" \
		-o match:"^${TMPDIR}/plop uroot groot 777 schg,nodump $" \
		-o match:"^${TMPDIR}/sym sym-root sym-gtoor 644 - sym-target$" \
		-e empty \
		-s exit:0 \
		pkg query "%Fp %Fu %Fg %Fm %Ff %Ft" test

	    atf_check \
		-o inline:"${TMPDIR}/test-dir uroot groot 660 arch,nodump\n" \
		-e empty \
		-s exit:0 \
		pkg query "%Sp %Su %Sg %Sm %Sf" test
	fi

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg delete -y plop

	atf_check \
		-o inline:"test 2\n" \
		-e empty \
		-s exit:0 \
		pkg query "%n %#O"

	atf_check \
		-o inline:"test 1\n" \
		-e empty \
		-s exit:0 \
		pkg query "%n %?O"

	atf_check \
		-o inline:"" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#O == 0" "%n"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test2.ucl

	sum1=$(pkg query -F ./test-1.pkg '%X')
	sum2=$(pkg query -F ./test-2.pkg '%X')

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		test "${sum1}" = "${sum2}"

	# Test 'pkg query -F'
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M plop.ucl

	atf_check \
		-o inline:"${TMPDIR}/plop\n${TMPDIR}/bla\n${TMPDIR}/sym\n" \
		-e empty \
		-s exit:0 \
		pkg query -F ./test-1.pkg '%Fp'

	atf_check \
		-o inline:"1\n" \
		-e empty \
		-s exit:0 \
		pkg query -F ./test-1.pkg '%?F'

	atf_check \
		-o inline:"3\n" \
		-e empty \
		-s exit:0 \
		pkg query -F ./test-1.pkg '%#F'

	atf_check \
		-o inline:"test 1\n" \
		-e empty \
		-s exit:0 \
		pkg query -F ./test-1.pkg '%n %v'

	atf_check \
		-o inline:"a test\n" \
		-e empty \
		-s exit:0 \
		pkg query -F ./test-1.pkg '%c'

	atf_check \
		-o inline:"Nothing to see here\n" \
		-e empty \
		-s exit:0 \
		pkg query -F ./plop-1.pkg '%c'
}
