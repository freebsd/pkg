#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	search \
	search_options \
	search_comment_description

search_body() {
	export REPOS_DIR=/nonexistent
	atf_check -e inline:"No active remote repositories configured.\n" -o empty -s exit:3 pkg -C '' -R '' search -e -Q comment -S name pkg
}

search_options_body() {
	touch pkgA.file
	cat << EOF > pkgA.ucl
name: pkgA
origin: misc/pkgA
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
deps: {
		pkgB: {
			origin: "misc/pkgB",
			version: "1.0"
		}
	}
	files: {
		${TMPDIR}/pkgA.file: "",
	}
EOF

	mkdir reposconf
	cat << EOF > reposconf/repos.conf
repoA: {
	url: file://${TMPDIR}/repoA,
	enabled: true
}
EOF

	pkg create -o ${TMPDIR}/repoA -M ./pkgA.ucl
	pkg repo -o ${TMPDIR}/repoA ${TMPDIR}/repoA

	OUTPUT_ORIGIN="misc/pkgA                      a test\n"
	OUTPUT_ORIGIN_QUIET="misc/pkgA\n"
	OUTPUT_QUIET_ONLY="pkgA-1.0\n"

	atf_check \
		-o inline:"${OUTPUT_ORIGIN}" \
		-e ignore \
		-s exit:0 \
	pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -o pkgA

	atf_check \
		-o inline:"${OUTPUT_ORIGIN_QUIET}" \
		-e ignore \
		-s exit:0 \
	pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -o -q pkgA

	atf_check \
		-o inline:"${OUTPUT_QUIET_ONLY}" \
		-e ignore \
		-s exit:0 \
	pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -q pkgA
}

search_comment_description_body() {
	# Test for issue #2118: search in both comment and description fields

	cat << EOF > alpha.ucl
name: alpha
origin: misc/alpha
version: "1.0"
maintainer: test
categories: [test]
comment: networking library
www: http://test
prefix: /usr/local
desc: <<EOD
A generic utility package
EOD
EOF

	cat << EOF > beta.ucl
name: beta
origin: misc/beta
version: "2.0"
maintainer: test
categories: [test]
comment: a generic tool
www: http://test
prefix: /usr/local
desc: <<EOD
Provides networking functions
EOD
EOF

	mkdir reposconf
	cat << EOF > reposconf/repos.conf
repo: {
	url: file://${TMPDIR}/repo,
	enabled: true
}
EOF

	for p in alpha beta; do
		pkg create -o ${TMPDIR}/repo -M ./${p}.ucl
	done
	pkg repo -o ${TMPDIR}/repo ${TMPDIR}/repo

	# Search by comment only: "networking" matches alpha's comment
	atf_check \
		-o match:"alpha" \
		-o not-match:"beta" \
		-e ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -S comment networking

	# Search by description only: "networking" matches beta's desc
	atf_check \
		-o match:"beta" \
		-o not-match:"alpha" \
		-e ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -S description networking

	# Search by comment-description: "networking" matches both
	atf_check \
		-o match:"alpha" \
		-o match:"beta" \
		-e ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -S comment-description networking
}
