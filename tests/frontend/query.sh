#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	query \
	query_empty_multiline \
	query_empty_provides_requires \
	query_provides_requires \
	query_purely_multiline_no_spurious \
	query_deps_follow_upgrade

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
		"mtime": 2342
	}
	"${TMPDIR}/bla": {
		"sum": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
		"uname": "utoor"
		"gname": "gtoor"
		"perm": "1765"
		"mtime": 4223
	}
	"${TMPDIR}/sym": {
		"sum": ""
		"uname": "sym-root"
		"gname": "sym-gtoor"
		"perm": "0644"
		"symlink_target": "sym-target"
		"mtime": 123432
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
		-o inline:"plop\n" \
		-e empty \
		-s exit:0 \
		pkg query -e '%dn = "test"' "%n"

	atf_check \
		-o inline:"test\n" \
		-e empty \
		-s exit:0 \
		pkg query -e '%dn != "test"' "%n"

	atf_check \
		-o inline:"plop\ntest\n" \
		-e empty \
		-s exit:0 \
		pkg query -e '%dn != "plop"' "%n"

	atf_check \
	    -o inline:"${TMPDIR}/test-dir\n" \
	    -e empty \
	    -s exit:0 \
	    pkg query "%D" test

	if [ "${OS}" = "FreeBSD" ]; then
	    atf_check \
		-o match:"^${TMPDIR}/bla utoor gtoor 1765 - 4223 $" \
		-o match:"^${TMPDIR}/plop uroot groot 777 schg,nodump 2342 $" \
		-o match:"^${TMPDIR}/sym sym-root sym-gtoor 644 - 123432 sym-target$" \
		-e empty \
		-s exit:0 \
		pkg query "%Fp %Fu %Fg %Fm %Ff %Fl %Ft" test

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
		-o inline:"${TMPDIR}/bla\n${TMPDIR}/plop\n${TMPDIR}/sym\n" \
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

query_empty_multiline_body() {
	# Packages without licenses should not be skipped by %L queries
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg nolic nolic 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg withlicense withlicense 1
	cat >> withlicense.ucl << EOF
licenses: ["BSD2CLAUSE"]
EOF

	atf_check -o ignore pkg register -M nolic.ucl
	atf_check -o ignore pkg register -M withlicense.ucl

	# %n without multiline: both packages listed
	atf_check \
		-o inline:"nolic\nwithlicense\n" \
		-s exit:0 \
		pkg query "%n"

	# %n %L: both packages must appear, nolic with empty license
	atf_check \
		-o inline:"nolic \nwithlicense BSD2CLAUSE\n" \
		-s exit:0 \
		pkg query "%n %L"

	# Same for categories: nolic has no categories in test_subr
	# but test_subr adds categories: [test], so both have one.
	# Test with shlibs instead (no package has shlibs)
	atf_check \
		-o inline:"nolic \nwithlicense \n" \
		-s exit:0 \
		pkg query "%n %B"
}

query_empty_provides_requires_body() {
	# Regression: pkg query "%n-%v %y" used to segfault when the
	# package had no provides (the multiline fallback passed NULL
	# data to fprintf("%s", NULL)). Same for %Y / requires.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg noprov noprov 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg withprov withprov 1
	cat >> withprov.ucl << EOF
provides: ["something"]
requires: ["otherthing"]
EOF

	atf_check -o ignore pkg register -M noprov.ucl
	atf_check -o ignore pkg register -M withprov.ucl

	# %n %y: noprov must appear with empty provides field, no crash
	atf_check \
		-o inline:"noprov \nwithprov something\n" \
		-s exit:0 \
		pkg query "%n %y"

	# Same for %Y / requires
	atf_check \
		-o inline:"noprov \nwithprov otherthing\n" \
		-s exit:0 \
		pkg query "%n %Y"

	# Mixed name-version + multiline (the exact failing case)
	atf_check \
		-o inline:"noprov-1 \nwithprov-1 something\n" \
		-s exit:0 \
		pkg query "%n-%v %y"
}

query_provides_requires_body() {
	# Nominal: package with multiple provides and requires entries.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg multi multi 1
	cat >> multi.ucl << EOF
provides: ["foo", "bar", "baz"]
requires: ["alpha", "beta"]
EOF

	atf_check -o ignore pkg register -M multi.ucl

	# Bare %y: one line per provide
	atf_check \
		-o inline:"bar\nbaz\nfoo\n" \
		-s exit:0 \
		pkg query "%y" multi

	# Bare %Y: one line per require
	atf_check \
		-o inline:"alpha\nbeta\n" \
		-s exit:0 \
		pkg query "%Y" multi

	# Mixed name + provides: name repeated on each line
	atf_check \
		-o inline:"multi bar\nmulti baz\nmulti foo\n" \
		-s exit:0 \
		pkg query "%n %y" multi

	# Mixed name-version + requires
	atf_check \
		-o inline:"multi-1 alpha\nmulti-1 beta\n" \
		-s exit:0 \
		pkg query "%n-%v %Y" multi

	# Count of provides / requires
	atf_check \
		-o inline:"3\n" \
		-s exit:0 \
		pkg query "%#y" multi

	atf_check \
		-o inline:"2\n" \
		-s exit:0 \
		pkg query "%#Y" multi

	# Boolean has-provides / has-requires
	atf_check \
		-o inline:"1\n" \
		-s exit:0 \
		pkg query "%?y" multi

	atf_check \
		-o inline:"1\n" \
		-s exit:0 \
		pkg query "%?Y" multi
}

query_purely_multiline_no_spurious_body() {
	# A purely multiline format should not produce spurious lines
	# for packages that have no matching multiline data (e.g., no deps).
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg nodeps nodeps 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg hasdeps hasdeps 1
	cat >> hasdeps.ucl << EOF
deps: {
    nodeps: {
        origin: "nodeps",
        version: "1"
    },
}
EOF

	atf_check -o ignore pkg register -M nodeps.ucl
	atf_check -o ignore pkg register -M hasdeps.ucl

	# Purely multiline format: only hasdeps has deps, so nodeps
	# must produce no output (no spurious " -" line)
	atf_check \
		-o inline:"nodeps nodeps-1\n" \
		-s exit:0 \
		pkg query "%do %dn-%dv"

	# Mixed format (%n + multiline %d): both packages must appear,
	# nodeps with empty dep fields via the fallback
	atf_check \
		-o inline:"hasdeps nodeps-1\nnodeps -\n" \
		-s exit:0 \
		pkg query "%n %dn-%dv"
}

query_deps_follow_upgrade_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg foo foo 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bar bar 1
	cat >> bar.ucl << EOF
deps: {
	foo: { origin: foo, version: "1" }
}
EOF
	atf_check pkg create -M foo.ucl
	atf_check pkg create -M bar.ucl
	atf_check -o ignore pkg -o REPOS_DIR=/dev/null install -y ./foo-1.pkg ./bar-1.pkg

	# upgrading foo, with a new origin, must update bar's dependency row
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf foo foo cat/foo 2
	atf_check pkg create -M foo.ucl
	atf_check -o ignore pkg -o REPOS_DIR=/dev/null install -y ./foo-2.pkg
	atf_check -o inline:"foo cat/foo 2\n" pkg query "%dn %do %dv" bar
	atf_check -o inline:"bar\n" pkg query -e "%dv == 2" %n
}
