#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	repo_v1 \
	repo_v2 \
	repo_multiversion \
	repo_multiformat \
	repo_symlinks \
	repo_content

repo_v1_body() {
	touch plop
	touch bla
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 "${TMPDIR}"
	cat >> test.ucl << EOF
files: {
	"${TMPDIR}/plop": ""
	"${TMPDIR}/bla": ""
}
EOF

	cat > meta.ucl << EOF
version = 1
EOF
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o inline:"WARNING: Meta v1 support will be removed in the next version\nCreating repository in .:  done\nPacking files for repository: WARNING: Meta v1 support will be removed in the next version\n done\n" \
		-e empty \
		-s exit:0 \
		pkg repo --meta-file meta.ucl .

	cp test-1.pkg test.pkg

	atf_check \
		-o inline:"WARNING: Meta v1 support will be removed in the next version\nCreating repository in .:  done\nPacking files for repository: WARNING: Meta v1 support will be removed in the next version\n done\n" \
		-e empty \
		-s exit:0 \
		pkg repo --meta-file meta.ucl .

	if [ `uname -s` = "Darwin" ]; then
		atf_pass
	fi

	nb=$(tar -xf digests.pkg -O digests | wc -l)
	atf_check_equal $nb 2

	mkdir Latest
	ln -s test-1.pkg Latest/test.pkg

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .
}
repo_v2_body() {
	touch plop
	touch bla
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 "${TMPDIR}"
	cat >> test.ucl << EOF
files: {
	"${TMPDIR}/plop": ""
	"${TMPDIR}/bla": ""
}
EOF

	cat > meta.ucl << EOF
version = 2
EOF
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo --meta-file meta.ucl .

	ln -s test-1.pkg test.pkg

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo --meta-file meta.ucl .

	if [ `uname -s` = "Darwin" ]; then
		atf_pass
	fi

	atf_check -s exit:127 -o ignore -e ignore "ls digest.pkg"

	mkdir Latest
	ln -s test-1.pkg Latest/test.pkg

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

}

repo_multiversion_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1.0 "${TMPDIR}"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test1 test 1.1 "${TMPDIR}"
	for i in test test1; do
		atf_check pkg create -M $i.ucl
	done

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		pkg repo .

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR} }
}
EOF

	atf_check -o ignore \
		pkg -C ./pkg.conf update

	# Ensure we can pickup the old version
	atf_check -o match:"Installing test-1\.0" \
		pkg -C ./pkg.conf install -y test-1.0

	atf_check -o match:"Upgrading.*to 1\.1" \
		pkg -C ./pkg.conf install -y test

	atf_check -o ignore pkg delete -y test

	atf_check -o match:"Installing test-1\.0" \
		pkg -C ./pkg.conf install -y test-1.0

	atf_check -o match:"Upgrading.*to 1\.1" \
		pkg -C ./pkg.conf upgrade -y

	atf_check -o ignore pkg -C ./pkg.conf delete -y test

	# Ensure the latest version is installed
	atf_check -o match:"Installing test-1.1" \
		pkg -C ./pkg.conf install -y test
}

repo_multiformat_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1.0 "${TMPDIR}"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg plop plop 1.1 "${TMPDIR}"
	atf_check pkg create -M test.ucl
	atf_check pkg create --format tar -M plop.ucl

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		pkg repo .

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR} }
}
EOF

	atf_check -o ignore \
		pkg -C ./pkg.conf update

	# Ensure we can pickup the old version
	atf_check -o match:"Installing test-1\.0" \
		pkg -C ./pkg.conf install -y test

	atf_check -o match:"Installing plop-1\.1" \
		pkg -C ./pkg.conf install -y plop
}

repo_symlinks_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1.0 "${TMPDIR}"
	atf_check pkg create --format txz -M test.ucl
	mkdir repo
	ln -sf ../test-1.0.pkg ./repo/meh-1.0.pkg
	atf_check -o ignore pkg repo repo
	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check -o ignore \
		pkg -C ./pkg.conf update
	atf_check -o inline:"test\n" \
		pkg -C ./pkg.conf rquery "%n"
	atf_check -o inline:"test\n" \
		pkg -C ./pkg.conf rquery -a "%n"
	atf_check -o inline:"test\n" \
		pkg -C ./pkg.conf rquery -e "%n == test" "%n"
	atf_check -o empty \
		pkg -C ./pkg.conf rquery -e "%n != test" "%n"
	atf_check -o inline:"test\n" \
		pkg -C ./pkg.conf rquery -e "%n == test" "%n" test
	atf_check -o empty \
		  -s exit:1 \
		pkg -C ./pkg.conf rquery -e "%n != test" "%n" test
	atf_check -o empty \
		  -s exit:1 \
		pkg -C ./pkg.conf rquery -e "%n == test" "%n" nottest

	rm -rf repo
	mkdir repo
	cp test-1.0.pkg repo/
	ln -fs test-1.0.pkg ./repo/meh-1.0.pkg

	atf_check -o ignore pkg repo repo
	atf_check -o ignore \
		pkg -C ./pkg.conf update -f
	atf_check -o inline:"test\n" \
		pkg -C ./pkg.conf rquery -a "%n"
}

repo_content_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1.0 "${TMPDIR}"
	atf_check pkg create --format txz -M test.ucl
	atf_check -o ignore pkg repo .
	nb=$(tar -xf packagesite.pkg -O - packagesite.yaml | wc -l)
	[ $nb -eq 1 ] || atf_fail "packagesite has $nb entries instead of 1"
}
