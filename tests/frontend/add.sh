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
		add_interleaved_requires_depends \
		add_overlong_archive_path \
		add_circular_depends \
		add_diamond_depends \
		add_circular_shlib_requires \
		add_circular_shlib_requires_depends \
		add_shlib_stdin_skip \
		add_shlib_dead_symlink \
		add_dir_attrs_preexisting \
		add_rootdir_symlink_escape

add_rootdir_symlink_escape_body() {
	# Package b ships an absolute symlink and package a installs a file
	# through it.  With -r the kernel would resolve that symlink on the
	# host, so pkg resolves it from the target root instead, as chroot
	# would: the target is re-rooted and nothing is written on the host.
	mkdir -p ${TMPDIR}/host/daily ${TMPDIR}/target
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg b b 1 /usr/local
	mkdir -p usr/local/etc
	ln -s ${TMPDIR}/host usr/local/etc/periodic
	echo /usr/local/etc/periodic > plist
	atf_check pkg create -M b.ucl -p plist -r .
	atf_check pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qy ${TMPDIR}/b-1.pkg
	test -L ${TMPDIR}/target/usr/local/etc/periodic || atf_fail "symlink not installed"

	rm usr/local/etc/periodic
	mkdir -p usr/local/etc/periodic/daily
	echo evil > usr/local/etc/periodic/daily/999.x
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg a a 1 /usr/local
	echo /usr/local/etc/periodic/daily/999.x > plist
	atf_check pkg create -M a.ucl -p plist -r .
	atf_check -s exit:0 \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qy ${TMPDIR}/a-1.pkg
	test -e ${TMPDIR}/host/daily/999.x && atf_fail "file written outside of the root"
	ls -A ${TMPDIR}/host/daily | grep -q pkgtemp && atf_fail "temporary file written outside of the root"
	test -f ${TMPDIR}/target${TMPDIR}/host/daily/999.x || \
	    atf_fail "file not installed inside the root"
	atf_check -o match:"^a-1" pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target info -q
}

add_dir_attrs_preexisting_body() {
	# A package must not alter the mode (nor ownership or flags) of a
	# directory it did not create, only of its own ones.
	mkdir -p ${TMPDIR}/target/usr/local/etc
	chmod 755 ${TMPDIR}/target/usr/local/etc
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 /usr/local
	mkdir -p usr/local/etc/newdir
	cat << EOF > plist
@dir(,,0777) /usr/local/etc
@dir(,,0700) /usr/local/etc/newdir
EOF
	atf_check pkg create -M test.ucl -p plist -r .
	atf_check pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qy ${TMPDIR}/test-1.pkg
	atf_check -o match:"^drwxr-xr-x" ls -ld ${TMPDIR}/target/usr/local/etc
	atf_check -o match:"^drwx------" ls -ld ${TMPDIR}/target/usr/local/etc/newdir

	# Having listed a directory in a previous version does not grant a
	# package the right to alter it later: pre-existing directories keep
	# their attributes, even the package's own ones.
	cat << EOF > plist
@dir(,,0777) /usr/local/etc
@dir(,,0750) /usr/local/etc/newdir
EOF
	atf_check pkg create -M test.ucl -p plist -r .
	atf_check pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg
	atf_check -o match:"^drwxr-xr-x" ls -ld ${TMPDIR}/target/usr/local/etc
	atf_check -o match:"^drwx------" ls -ld ${TMPDIR}/target/usr/local/etc/newdir
}

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

add_interleaved_requires_depends_body() {
	# pkg1 depends on pkg2
	# pkg2 requires from pkg3
	# pkg3 depends on pkg4
	# pkg4 requires from pkg5
	# test that "pkg add pkg1" works
	setup_provider_layout

	# pkg5
	cc -shared -Wl,-soname=libpkg5.so.1 -Wl,--no-as-needed \
		empty.c -o libpkg5.so.1
	ln -s libpkg5.so.1 libpkg5.so
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg5 pkg5 1 /usr/local
	cat << EOF >> pkg5.ucl
files: {
	$(pwd)/libpkg5.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M pkg5.ucl
	mv pkg5-1.pkg All/
	mkdir -p shlibs/libpkg5.so.1
	ln -s ../../All/pkg5-1.pkg shlibs/libpkg5.so.1/pkg5.pkg

	# pkg4
	cc -shared -Wl,-soname=libpkg4.so.1 -Wl,--no-as-needed \
		empty.c -o libpkg4.so.1 -L. -lpkg5
	ln -s libpkg4.so.1 libpkg4.so
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg4 pkg4 1 /usr/local
	cat << EOF >> pkg4.ucl
files: {
	$(pwd)/libpkg4.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M pkg4.ucl
	mv pkg4-1.pkg All/
	mkdir -p shlibs/libpkg4.so.1
	ln -s ../../All/pkg4-1.pkg shlibs/libpkg4.so.1/pkg4.pkg

	# pkg3
	cc -shared -Wl,-soname=libpkg3.so.1 -Wl,--no-as-needed \
		empty.c -o libpkg3.so.1
	ln -s libpkg3.so.1 libpkg3.so
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg3 pkg3 1 /usr/local
	cat << EOF >> pkg3.ucl
deps: {
	pkg4: {
		origin: "pkg4",
		version: "1"
	}
}
files: {
	$(pwd)/libpkg3.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M pkg3.ucl
	mv pkg3-1.pkg All/
	mkdir -p shlibs/libpkg3.so.1
	ln -s ../../All/pkg3-1.pkg shlibs/libpkg3.so.1/pkg3.pkg

	# pkg2
	cc -shared -Wl,-soname=libpkg2.so.1 -Wl,--no-as-needed \
		empty.c -o libpkg2.so.1 -L. -lpkg3
	ln -s libpkg2.so.1 libpkg2.so
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg2 pkg2 1 /usr/local
	cat << EOF >> pkg2.ucl
files: {
	$(pwd)/libpkg2.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M pkg2.ucl
	mv pkg2-1.pkg All/
	mkdir -p shlibs/libpkg2.so.1
	ln -s ../../All/pkg2-1.pkg shlibs/libpkg2.so.1/pkg2.pkg

	# pkg1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 pkg1 1 /usr/local
	cat << EOF >> pkg1.ucl
deps: {
	pkg2: {
		origin: "pkg2",
		version: "1"
	}
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M pkg1.ucl
	mv pkg1-1.pkg All/

	# Install pkg1 and all up to pkg5 should get installed
	atf_check \
		-o match:"Extracting pkg5-1:.*done" \
		-e empty \
		-s exit:0 \
		pkg add $(pwd)/All/pkg1-1.pkg
}

add_overlong_archive_path_body() {
	# an archive entry whose path does not fit in MAXPATHLEN must be
	# reported, not extracted under a truncated name
	pathmax=$(getconf PATH_MAX / 2>/dev/null || echo 4096)
	long=$(awk -v n=$((pathmax + 100)) \
	    'BEGIN { for (i = 0; i < n; i++) printf "a" }')

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat << EOF >> test.ucl
files: {
	/usr/local/share/ok: ""
}
EOF
	mkdir -p stage/usr/local/share
	: > stage/usr/local/share/ok
	atf_check -o ignore -s exit:0 pkg create -M test.ucl -r stage -o .

	mkdir ext
	atf_check -o ignore -e ignore -s exit:0 bsdtar -xf test-1.pkg -C ext
	: > ext/x
	atf_check -o ignore -e ignore -s exit:0 bsdtar --format pax -cf test-long.pkg \
	    -s "|^x\$|/usr/local/${long}|" -C ext +MANIFEST x

	atf_check \
		-o ignore \
		-e match:"Path too long in the archive" \
		-s not-exit:0 \
		pkg add $(pwd)/test-long.pkg
}

add_circular_depends_body() {
	# Two packages having "deps" on each other should not make
	# "pkg add" recurse infinitely
	setup_provider_layout

	# pkg1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 pkg1 1 /usr/local
	cat << EOF >> pkg1.ucl
deps: {
	pkg2: {
		origin: "pkg2",
		version: "1"
	}
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M pkg1.ucl
	mv pkg1-1.pkg All/

	# pkg2
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg2 pkg2 1 /usr/local
	cat << EOF >> pkg2.ucl
deps: {
	pkg1: {
		origin: "pkg1",
		version: "1"
	}
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M pkg2.ucl
	mv pkg2-1.pkg All/

	# Install pkg1
	atf_check \
		-o ignore \
		-e match:"dependency cycle detected" \
		-s exit:1 \
		pkg add $(pwd)/All/pkg1-1.pkg
}

add_diamond_depends_body() {
	# A dependency graph which is not a cycle: a -> b, a -> c, b -> d and
	# c -> d.  d is reached twice, which must not be reported as a cycle.
	setup_provider_layout

	for pkg in a b c d; do
		atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg \
		    ${pkg} ${pkg} 1 /usr/local
		case ${pkg} in
		a)
			cat << EOF >> ${pkg}.ucl
deps: {
	b: { origin: "b", version: "1" }
	c: { origin: "c", version: "1" }
}
EOF
			;;
		b|c)
			cat << EOF >> ${pkg}.ucl
deps: {
	d: { origin: "d", version: "1" }
}
EOF
			;;
		esac
		atf_check -o ignore -e empty -s exit:0 \
		    pkg create -M ${pkg}.ucl
		mv ${pkg}-1.pkg All/
	done

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg add $(pwd)/All/a-1.pkg

	atf_check \
		-o inline:"a\nb\nc\nd\n" \
		-e empty \
		-s exit:0 \
		sh -c 'pkg query %n | sort'
}

add_circular_shlib_requires_body() {
	# Same as add_circular_depends(), but both packages depend on each other
	# via shlib "requires".
	atf_skip_on Darwin "The macOS linker uses different flags"
	atf_skip_on Linux "On Linux (debian-like) the library are not on the scanned path for shlibs"
	setup_provider_layout
	rm All/provider-1.pkg
	rm -rf shlibs/libtest.so.1

	ln -s libconsumer.so.1 libconsumer.so
	cc -shared -Wl,-soname=libconsumerconsumer.so.1 -Wl,--no-as-needed \
		empty.c -o libconsumerconsumer.so.1 -L. -lconsumer

	# Package 1: contains libconsumerconsumer.so.1 that requires libconsumer.so.1 from Package 2,
	#            also contains libtest.so.1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg package1 package1 1 /usr/local
	cat << EOF >> package1.ucl
files: {
	$(pwd)/libconsumerconsumer.so.1: "",
	$(pwd)/libtest.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M package1.ucl
	mv package1-1.pkg All/

	# Package 2: contains libconsumer.so.1 that requires libtest.so.1 from Package 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg package2 package2 1 /usr/local
	cat << EOF >> package2.ucl
files: {
	$(pwd)/libconsumer.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M package2.ucl
	mv package2-1.pkg All/

	# Extend symlink directory
	mkdir -p shlibs/libconsumer.so.1
	ln -s ../../All/package2-1.pkg shlibs/libconsumer.so.1/package2.pkg
	mkdir -p shlibs/libtest.so.1
	ln -s ../../All/package2-1.pkg shlibs/libtest.so.1/package2.pkg
	mkdir -p shlibs/libconsumerconsumer.so.1
	ln -s ../../All/package1-1.pkg shlibs/libconsumerconsumer.so.1/package1.pkg

	# Install Package 1 and make sure we don't infinitely recurse
	atf_check \
		-o ignore \
		-e match:"dependency cycle detected" \
		-s exit:1 \
		pkg add $(pwd)/All/package1-1.pkg

	# Try the same starting from Package 2
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg delete -a
	atf_check \
		-o ignore \
		-e match:"dependency cycle detected" \
		-s exit:1 \
		pkg add $(pwd)/All/package2-1.pkg
}

add_circular_shlib_requires_depends_body() {
	# Same as add_circular_depends(), but one package depends via "deps" and
	# the other one depends via "requires".
	atf_skip_on Darwin "The macOS linker uses different flags"
	atf_skip_on Linux "On Linux (debian-like) the library are not on the scanned path for shlibs"
	setup_provider_layout
	rm All/provider-1.pkg
	rm -rf shlibs/libtest.so.1

	ln -s libconsumer.so.1 libconsumer.so
	cc -shared -Wl,-soname=libconsumerconsumer.so.1 -Wl,--no-as-needed \
		empty.c -o libconsumerconsumer.so.1 -L. -lconsumer

	# Package 1: contains libconsumerconsumer.so.1 that requires libconsumer.so.1 from Package 2
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg package1 package1 1 /usr/local
	cat << EOF >> package1.ucl
files: {
	$(pwd)/libconsumerconsumer.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M package1.ucl
	mv package1-1.pkg All/

	# Package 2: contains libconsumer.so.1 and depends on Package 1
	# libtest is here to fulfill the libconsumer.so requirement
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg package2 package2 1 /usr/local
	cat << EOF >> package2.ucl
files: {
	$(pwd)/libconsumer.so.1: "",
	$(pwd)/libtest.so.1: "",
}
deps: {
	package1: {
		origin: "package1",
		version: "1"
	}
}
EOF
	atf_check -o ignore -e empty -s exit:0 \
		pkg create -M package2.ucl
	mv package2-1.pkg All/

	# Extend symlink directory
	mkdir -p shlibs/libconsumer.so.1
	ln -s ../../All/package2-1.pkg shlibs/libconsumer.so.1/package2.pkg
	mkdir -p shlibs/libconsumerconsumer.so.1
	ln -s ../../All/package1-1.pkg shlibs/libconsumerconsumer.so.1/package1.pkg

	# Install Package 1 and make sure we don't infinitely recurse
	atf_check \
		-o ignore \
		-e match:"dependency cycle detected" \
		-s exit:1 \
		pkg add $(pwd)/All/package1-1.pkg

	# Try the same starting from Package 2
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg delete -a
	atf_check \
		-o ignore \
		-e match:"dependency cycle detected" \
		-s exit:1 \
		pkg add $(pwd)/All/package2-1.pkg
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
