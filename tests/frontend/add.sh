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
		add_no_version \
		add_no_version_multi \
		add_deps_multi \
		add_wrong_version \
		add_shlib_provider \
		add_shlib_priority \
		add_shlib_missing \
		add_shlib_accept_missing \
		add_shlib_already_installed \
		add_provides_requires \
		add_shlib_stdin_skip \
		add_shlib_dead_symlink

initialize_pkg() {
	touch a
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat << EOF >> test.ucl
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
		pkg add test-1.pkg

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
		pkg add -A test-1.pkg

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
	cat test-1.pkg | atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		pkg add -I test-1.pkg
}

add_force_body() {
	initialize_pkg
}


add_accept_missing_body() {
	touch a
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat << EOF >> test.ucl
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
		-o inline:"${JAILED}Installing test-1...\n\nFailed to install the following 1 package(s): test-1.pkg\n" \
		-e inline:"${PROGNAME}: Missing dependency 'b'\n" \
		-s exit:1 \
		pkg add test-1.pkg

OUTPUT="${JAILED}Installing test-1...
pre-install
${JAILED}Extracting test-1:  done
post-install
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e inline:"${PROGNAME}: Missing dependency 'b'\n" \
		-s exit:0 \
		pkg add -M test-1.pkg
}

add_quiet_body() {
	initialize_pkg

	atf_check \
		-o inline:"pre-install\npost-install\n" \
		-e empty \
		pkg add -q ./test-1.pkg
}

add_stdin_body() {
	initialize_pkg

OUTPUT="${JAILED}Installing test-1...
pre-install
${JAILED}Extracting test-1:  done
post-install
"
	cat test-1.pkg | atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		pkg add -
}

add_stdin_missing_body() {
	touch a
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat << EOF >> test.ucl
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
		-o inline:"${JAILED}Installing test-1...\n\nFailed to install the following 1 package(s): -\n" \
		-e inline:"${PROGNAME}: Missing dependency 'b'\n" \
		-s exit:1 \
		pkg add - < test-1.pkg

OUTPUT="${JAILED}Installing test-1...
pre-install
${JAILED}Extracting test-1:  done
post-install
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e inline:"${PROGNAME}: Missing dependency 'b'\n" \
		-s exit:0 \
		pkg add -M - < test-1.pkg
}

add_no_version_body() {
	for p in test final ; do
		atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ${p} ${p} 1
		if [ ${p} = "final" ]; then
			cat << EOF >> final.ucl
deps {
	test {
		origin = "test";
	}
}
EOF
		fi
		atf_check -o ignore -s exit:0 \
			pkg create -M ${p}.ucl
	done
	atf_check -o ignore -s exit:0 \
		pkg add final-1.pkg
}

add_no_version_multi_body() {
	for p in test final ; do
		atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ${p} ${p} 1
		if [ ${p} = "final" ]; then
			cat << EOF >> final.ucl
deps {
	test {
		origin = "test";
	},
	pkgnotfound {
		origin = "pkgnotfound";
	}
}
EOF
		fi
		atf_check -o ignore -s exit:0 \
			pkg create -M ${p}.ucl
	done
	atf_check -o ignore -e ignore -s exit:1 \
		pkg add final-1.pkg
}

add_deps_multi_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 2
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg final final 1

	cat << EOF >> final.ucl
deps {
	test {
		origin = "test";
	},
}
EOF
	atf_check -o ignore -s exit:0 pkg create -M test.ucl
	atf_check -o ignore -s exit:0 pkg create -M final.ucl
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	atf_check -o ignore -s exit:0 pkg create -M test.ucl
	atf_check -o "match:.*test-2.*" -e empty -s exit:0 \
		pkg add final-1.pkg
}

add_require_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg final final 1
	cat << EOF >> final.ucl
requires: [functionA]
EOF
	cat << EOF >> test.ucl
provides: [functionA]
EOF

	atf_check -o ignore -s exit:0 pkg create -M test.ucl
	atf_check  -s exit:0 pkg create -M final.ucl
	atf_check -o match:".*test-1.*" -e ignore -s exit:0 \
		pkg add final-1.pkg
}

add_wrong_version_body() {
	for p in test final ; do
		atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ${p} ${p} 1
		if [ ${p} = "final" ]; then
			cat << EOF >> final.ucl
deps {
	test {
		origin = "test";
		version = "2";
	}
}
EOF
		fi
		atf_check -o ignore -s exit:0 \
			pkg create -M ${p}.ucl
	done
	atf_check -o ignore -s exit:0 \
		pkg add final-1.pkg
}

# Helper: create shared libraries for shlib tests.
# Creates libtest.so.1 (provider) and libconsumer.so.1 (requires libtest.so.1).
create_shlibs() {
	touch empty.c
	cc -shared -Wl,-soname=libtest.so.1 empty.c -o libtest.so.1
	ln -s libtest.so.1 libtest.so
	cc -shared -Wl,-soname=libconsumer.so.1 -Wl,--no-as-needed \
		empty.c -o libconsumer.so.1 -L. -ltest
}

# Helper: set up the symlink directory layout for shlib tests.
# Creates real shared libs, packages in All/, and symlink dir.
setup_provider_layout() {
	atf_skip_on Darwin "The macOS linker uses different flags"
	atf_skip_on Linux "On linux (debian-like) the library are not on the scanned path for shlibs"
	mkdir -p All
	create_shlibs

	# Provider package: contains libtest.so.1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg provider provider 1 /usr/local
	cat << EOF >> provider.ucl
files: {
	$(pwd)/libtest.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M provider.ucl
	mv provider-1.pkg All/

	# Consumer package: contains libconsumer.so.1 (requires libtest.so.1)
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg consumer consumer 1 /usr/local
	cat << EOF >> consumer.ucl
files: {
	$(pwd)/libconsumer.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M consumer.ucl
	mv consumer-1.pkg All/

	# Set up symlink directory
	mkdir -p shlibs/libtest.so.1
	ln -s ../../All/provider-1.pkg shlibs/libtest.so.1/provider.pkg
}


add_shlib_provider_body() {
	setup_provider_layout

	atf_check \
		-o match:"Installing provider-1" \
		-o match:"Installing consumer-1" \
		-e empty \
		-s exit:0 \
		pkg add $(pwd)/All/consumer-1.pkg

	# Verify both packages are installed
	atf_check -o inline:"consumer\nprovider\n" -e empty -s exit:0 \
		pkg query -a "%n"

	# Verify provider was installed as automatic
	atf_check -o inline:"1\n" -e empty -s exit:0 \
		pkg query "%a" provider
}


add_shlib_priority_body() {
	atf_skip_on Darwin "The macOS linker uses different flags"
	atf_skip_on Linux "On Linux (debian-like) the library are not on the scanned path for shlibs"
	mkdir -p All
	create_shlibs

	# Create two provider packages (both provide libtest.so.1)
	for p in alpha bravo; do
		atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ${p} ${p} 1 /usr/local
		cat << EOF >> ${p}.ucl
files: {
	$(pwd)/libtest.so.1: "",
}
EOF
		atf_check -o ignore -e empty -s exit:0 \
			pkg create -M ${p}.ucl
		mv ${p}-1.pkg All/
	done

	# Consumer with libconsumer.so.1 (requires libtest.so.1)
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg consumer consumer 1 /usr/local
	cat << EOF >> consumer.ucl
files: {
	$(pwd)/libconsumer.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M consumer.ucl
	mv consumer-1.pkg All/

	# Symlink dir with priority prefixes: 00. = highest priority
	mkdir -p shlibs/libtest.so.1
	ln -s ../../All/bravo-1.pkg shlibs/libtest.so.1/00.bravo.pkg
	ln -s ../../All/alpha-1.pkg shlibs/libtest.so.1/01.alpha.pkg

	# 00.bravo.pkg sorts first alphabetically → bravo wins
	atf_check \
		-o match:"Installing bravo-1" \
		-o match:"Installing consumer-1" \
		-o not-match:"Installing alpha-1" \
		-e empty \
		-s exit:0 \
		pkg add $(pwd)/All/consumer-1.pkg
}

add_shlib_missing_body() {
	atf_skip_on Darwin "The macOS linker uses different flags"
	atf_skip_on Linux "On Linux (debian-like) the library are not on the scanned path for shlibs"
	mkdir -p All
	create_shlibs

	# Consumer with libconsumer.so.1 (requires libtest.so.1)
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg consumer consumer 1 /usr/local
	cat << EOF >> consumer.ucl
files: {
	$(pwd)/libconsumer.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M consumer.ucl
	mv consumer-1.pkg All/

	# No shlibs/ directory at all → should fail
	atf_check \
		-o ignore \
		-e match:"Missing shlib libtest.so.1 required by consumer" \
		-s exit:1 \
		pkg add $(pwd)/All/consumer-1.pkg
}

add_shlib_accept_missing_body() {
	atf_skip_on Darwin "The macOS linker uses different flags"
	atf_skip_on Linux "On Linux (debian-like) the library are not on the scanned path for shlibs"
	mkdir -p All
	create_shlibs

	# Consumer with libconsumer.so.1 (requires libtest.so.1)
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg consumer consumer 1 /usr/local
	cat << EOF >> consumer.ucl
files: {
	$(pwd)/libconsumer.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M consumer.ucl
	mv consumer-1.pkg All/

	# With -M (accept missing), should succeed despite missing shlib
	atf_check \
		-o match:"Installing consumer-1" \
		-e match:"Missing shlib libtest.so.1 required by consumer" \
		-s exit:0 \
		pkg add -M $(pwd)/All/consumer-1.pkg
}

add_shlib_already_installed_body() {
	setup_provider_layout

	# Pre-install the provider
	atf_check \
		-o match:"Installing provider-1" \
		-e empty \
		-s exit:0 \
		pkg add $(pwd)/All/provider-1.pkg

	# Now install the consumer; provider shlib is already satisfied
	# so provider should NOT be re-installed
	atf_check \
		-o match:"Installing consumer-1" \
		-o not-match:"Installing provider-1" \
		-e empty \
		-s exit:0 \
		pkg add $(pwd)/All/consumer-1.pkg
}

add_provides_requires_body() {
	mkdir -p All

	# Provider with abstract provides
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg myprovider myprovider 1 /usr/local
	cat << EOF >> myprovider.ucl
provides: [
	"vi-editor",
]
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M myprovider.ucl
	mv myprovider-1.pkg All/

	# Consumer with abstract requires
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg consumer consumer 1 /usr/local
	cat << EOF >> consumer.ucl
requires: [
	"vi-editor",
]
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M consumer.ucl
	mv consumer-1.pkg All/

	# Set up provides symlink directory
	mkdir -p provides/vi-editor
	ln -s ../../All/myprovider-1.pkg provides/vi-editor/myprovider.pkg

	atf_check \
		-o match:"Installing myprovider-1" \
		-o match:"Installing consumer-1" \
		-e empty \
		-s exit:0 \
		pkg add $(pwd)/All/consumer-1.pkg

	# Verify both installed
	atf_check -o inline:"consumer\nmyprovider\n" -e empty -s exit:0 \
		pkg query -a "%n"
}

add_shlib_stdin_skip_body() {
	setup_provider_layout

	# From stdin, shlib resolution should be skipped entirely
	# (no directory context to search), so it should succeed
	# without trying to resolve shlibs
	atf_check \
		-o match:"Installing consumer-1" \
		-e empty \
		-s exit:0 \
		pkg add - < All/consumer-1.pkg
}

add_shlib_dead_symlink_body() {
	atf_skip_on Darwin "The macOS linker uses different flags"
	atf_skip_on Linux "On Linux (debian-like) the library are not on the scanned path for shlibs"
	mkdir -p All
	create_shlibs

	# Provider package
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg provider provider 1 /usr/local
	cat << EOF >> provider.ucl
files: {
	$(pwd)/libtest.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M provider.ucl
	mv provider-1.pkg All/

	# Consumer package
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg consumer consumer 1 /usr/local
	cat << EOF >> consumer.ucl
files: {
	$(pwd)/libconsumer.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M consumer.ucl
	mv consumer-1.pkg All/

	# Symlink dir with one dead symlink and one valid one
	mkdir -p shlibs/libtest.so.1
	ln -s ../../All/nonexistent-1.pkg shlibs/libtest.so.1/dead.pkg
	ln -s ../../All/provider-1.pkg shlibs/libtest.so.1/provider.pkg

	# Dead symlink should be ignored, valid provider used
	atf_check \
		-o match:"Installing provider-1" \
		-o match:"Installing consumer-1" \
		-e empty \
		-s exit:0 \
		pkg add $(pwd)/All/consumer-1.pkg
}
