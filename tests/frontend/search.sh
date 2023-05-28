#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	search \
	search_options

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
