#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	search \
	search_any \
	search_glob_fallback \
	search_invalid_regex \
	search_multiple_json_compact \
	search_options \
	search_comment_description

search_body() {
	export REPOS_DIR=/nonexistent
	atf_check -e inline:"No active remote repositories configured.\n" -o empty -s exit:3 pkg -C '' -R '' search -e -Q comment -S name pkg
}

search_multiple_json_compact_body() {
	atf_require python3 "Requires python3 to run this test"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "testa" "testa" "1.0"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "testb" "testb" "1.0"
	atf_check -s exit:0 pkg create -o "${TMPDIR}/repo" -M ./testa.ucl
	atf_check -s exit:0 pkg create -o "${TMPDIR}/repo" -M ./testb.ucl
	atf_check -s exit:0 -o ignore pkg repo "${TMPDIR}/repo"
	atf_check -s exit:0 mkdir reposconf
	cat << EOF > reposconf/repos.conf
repo: {
	url: file://${TMPDIR}/repo,
	enabled: true
}
EOF
	atf_check -s exit:0 -o save:out.json pkg --repo-conf-dir "${TMPDIR}/reposconf" search -R --raw-format json-compact test
	atf_check -s exit:0 -o ignore -e empty python3 -m json.tool out.json
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

search_invalid_regex_body() {
	# An invalid regular expression must be reported once, as a user error,
	# and not once per repository by the SQL layer (issue #2751).

	for r in repoA repoB; do
		mkdir ${r}
		cat << EOF > ${r}.ucl
name: pkg${r}
origin: misc/pkg${r}
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
a test
EOD
EOF
		pkg create -o ${r} -M ./${r}.ucl
		pkg repo -o ${r} ${r}
	done

	mkdir reposconf
	cat << EOF > reposconf/repos.conf
repoA: {
	url: file://${TMPDIR}/repoA,
	enabled: true
}
repoB: {
	url: file://${TMPDIR}/repoB,
	enabled: true
}
EOF

	# An explicitly requested regular expression which does not compile is
	# reported once, and not retried as a glob.
	atf_check \
		-o empty \
		-e save:stderr \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -x '*rust'

	atf_check -o inline:"1\n" -e empty -s exit:0 \
		grep -c "Invalid regular expression" stderr

	# The message must not mention the SQL layer nor an internal file.
	atf_check -o empty -e empty -s exit:1 \
		grep -E "sqlite|pkgdb_iterator" stderr

	# The commands matching the installed packages report it the same way.
	atf_check \
		-o empty \
		-e save:stderr \
		-s exit:1 \
		pkg info -x '*rust'

	atf_check -o inline:"1\n" -e empty -s exit:0 \
		grep -c "Invalid regular expression" stderr
}

search_glob_fallback_body() {
	# A pattern which looks like a shell glob but is not a valid regular
	# expression is matched as a glob when no match mode is requested.

	for p in rust rustup py-rust mysql80-client; do
		cat << EOF > ${p}.ucl
name: "${p}"
origin: "misc/${p}"
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
a test
EOD
EOF
		atf_check -o ignore pkg create -o ${TMPDIR}/repo -M ./${p}.ucl
	done
	atf_check -o ignore pkg repo -o ${TMPDIR}/repo ${TMPDIR}/repo

	mkdir reposconf
	cat << EOF > reposconf/repos.conf
repo: {
	url: file://${TMPDIR}/repo,
	enabled: true
}
EOF

	# '*rust' is not a valid regular expression, so it is matched as a
	# glob: the packages whose name ends with rust.
	atf_check \
		-o inline:"py-rust-1.0\nrust-1.0\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -q '*rust'

	# A pattern which is a valid regular expression keeps its meaning.
	atf_check \
		-o match:"rustup" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -q 'rust*'

	# The package name is matched, and not only the name and the version.
	atf_check \
		-o inline:"rust-1.0\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -q -e rust

	atf_check \
		-o inline:"py-rust-1.0\nrust-1.0\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -q -g '*rust'
}

# A package can be searched by name, origin, comment and description at once,
# the way the ports tree searches its index by keyword (issue #2700).
search_any_body()
{
	cat << EOF > zzz.ucl
name: zzz
origin: misc/zzz
version: "1.0"
maintainer: test
categories: [test]
comment: a tool to search bone
www: http://test
prefix: /usr/local
desc: <<EOD
A tool which digs deep into the marrow of things.
EOD
EOF

	mkdir reposconf
	cat << EOF > reposconf/repos.conf
repo: {
	url: file://${TMPDIR}/repo,
	enabled: true
}
EOF

	atf_check -o ignore pkg create -o ${TMPDIR}/repo -M ./zzz.ucl
	atf_check -o ignore pkg repo -o ${TMPDIR}/repo ${TMPDIR}/repo

	# The name, the origin, the comment and the description.
	for field in zzz test bone marrow; do
		atf_check \
			-o match:"zzz-1.0" \
			-e ignore \
			-s exit:0 \
			pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -S any ${field}
	done

	atf_check \
		-o empty \
		-e empty \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" search -S any nothere
}
