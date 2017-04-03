#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init	\
		add \
		add_automatic \
		add_noscript \
		add_noscript \
		add_force \
		add_accept_missing \
		add_quiet \
		add_stdin \
		add_stdin_missing \
		add_no_version

initialize_pkg() {
	touch a
	cat << EOF > test.ucl
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
files: {
	${TMPDIR}/a: ""
}
scripts: {
	pre-install: <<EOD
echo "pre-install"
EOD
	post-install: <<EOD
echo "post-install"
EOD
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl
}

add_body() {
	initialize_pkg

OUTPUT="${JAILED}Installing test-1...
pre-install
${JAILED}Extracting test-1:  done
post-install
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		pkg add test-1.txz

# test automatic is not set
	atf_check \
		-o inline:"0\n" \
		-e empty \
		pkg query "%a" test
}

add_automatic_body() {
	initialize_pkg

OUTPUT="${JAILED}Installing test-1...
pre-install
${JAILED}Extracting test-1:  done
post-install
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		pkg add -A test-1.txz

	atf_check \
		-o inline:"1\n" \
		-e empty \
		pkg query "%a" test

}

add_noscript_body() {
	initialize_pkg

OUTPUT="${JAILED}Installing test-1...
${JAILED}Extracting test-1:  done
"
	cat test-1.txz | atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		pkg add -I test-1.txz
}

add_force_body() {
	initialize_pkg
}


add_accept_missing_body() {
	touch a
	cat << EOF > test.ucl
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
deps: {
	b: {
		origin: "wedontcare",
		version: "1"
	}
}
files: {
	${TMPDIR}/a: ""
}
scripts: {
	pre-install: <<EOD
echo "pre-install"
EOD
	post-install: <<EOD
echo "post-install"
EOD
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o inline:"${JAILED}Installing test-1...\n\nFailed to install the following 1 package(s): test-1.txz\n" \
		-e inline:"${PROGNAME}: Missing dependency 'b'\n" \
		-s exit:70 \
		pkg add test-1.txz

OUTPUT="${JAILED}Installing test-1...
pre-install
${JAILED}Extracting test-1:  done
post-install
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e inline:"${PROGNAME}: Missing dependency 'b'\n" \
		-s exit:0 \
		pkg add -M test-1.txz
}

add_quiet_body() {
	initialize_pkg

	atf_check \
		-o inline:"pre-install\npost-install\n" \
		-e empty \
		pkg add -q ./test-1.txz
}

add_stdin_body() {
	initialize_pkg

OUTPUT="${JAILED}Installing test-1...
pre-install
${JAILED}Extracting test-1:  done
post-install
"
	cat test-1.txz | atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		pkg add -
}

add_stdin_missing_body() {
	touch a
	cat << EOF > test.ucl
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
deps: {
	b: {
		origin: "wedontcare",
		version: "1"
	}
}
files: {
	${TMPDIR}/a: ""
}
scripts: {
	pre-install: <<EOD
echo "pre-install"
EOD
	post-install: <<EOD
echo "post-install"
EOD
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	cat test-1.txz | atf_check \
		-o inline:"${JAILED}Installing test-1...\n\nFailed to install the following 1 package(s): -\n" \
		-e inline:"${PROGNAME}: Missing dependency 'b'\n" \
		-s exit:70 \
		pkg add -

OUTPUT="${JAILED}Installing test-1...
pre-install
${JAILED}Extracting test-1:  done
post-install
"
	cat test-1.txz | atf_check \
		-o inline:"${OUTPUT}" \
		-e inline:"${PROGNAME}: Missing dependency 'b'\n" \
		-s exit:0 \
		pkg add -M -
}

add_no_version_body() {
	cat << EOF > test.ucl
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

	cat << EOF > test-lib.ucl
name: test-lib
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

	cat << EOF > final.ucl
name: final
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
deps {
	test {
		origin = "test";
	}
}
EOF
	for p in test test-lib final ; do
		atf_check -o ignore -s exit:0 \
			pkg create -M ${p}.ucl
	done
	atf_check -o ignore -s exit:0 \
		pkg add final-1.txz
}
