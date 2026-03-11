#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	rquery_name_version \
	rquery_all \
	rquery_comment \
	rquery_origin \
	rquery_prefix \
	rquery_deps \
	rquery_rdeps \
	rquery_options \
	rquery_categories \
	rquery_size \
	rquery_eval \
	rquery_eval_complex \
	rquery_glob \
	rquery_regex \
	rquery_no_repo \
	rquery_not_found \
	rquery_multiple_pkgs \
	rquery_multi_repo

# Helper: set up a local file:// repo with rich packages
setup_repo() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "dep" "dep" "1.0" "/usr/local"
	cat << EOF >> dep.ucl
categories: [net]
EOF
	atf_check pkg create -o ${TMPDIR}/repo -M dep.ucl

	cat << EOF > test.ucl
name: test
origin: test
version: "2.5"
maintainer: test
categories: [devel, test]
comment: a test
www: http://test
prefix: /usr/local
abi: "*"
desc: <<EOD
This is a test
EOD
deps: {
    dep: {
        origin: dep,
        version: "1.0"
    }
}
options: {
    "OPT1": "on",
    "OPT2": "off",
}
EOF
	atf_check pkg create -o ${TMPDIR}/repo -M test.ucl

	atf_check -o ignore pkg repo ${TMPDIR}/repo

	mkdir -p ${TMPDIR}/reposconf
	cat << EOF > ${TMPDIR}/reposconf/test.conf
test: {
    url: file://${TMPDIR}/repo,
    enabled: true
}
EOF

	atf_check -o ignore \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		update

	RDIR="${TMPDIR}/reposconf"
}

rquery_name_version_body() {
	setup_repo

	# %n %v: name and version
	atf_check \
		-o inline:"test-2.5\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%n-%v" test

	atf_check \
		-o inline:"dep-1.0\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%n-%v" dep
}

rquery_all_body() {
	setup_repo

	# -a: all packages
	atf_check \
		-o match:"dep" \
		-o match:"test" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -Ua "%n"
}

rquery_comment_body() {
	setup_repo

	# %c: comment
	atf_check \
		-o inline:"a test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%c" test
}

rquery_origin_body() {
	setup_repo

	# %o: origin
	atf_check \
		-o inline:"test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%o" test
}

rquery_prefix_body() {
	setup_repo

	# %p: prefix
	atf_check \
		-o inline:"/usr/local\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%p" test
}

rquery_deps_body() {
	setup_repo

	# %dn %dv: dependency name and version
	atf_check \
		-o inline:"dep 1.0\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%dn %dv" test

	# %#d: dep count
	atf_check \
		-o inline:"1\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%#d" test

	# dep has no deps
	atf_check \
		-o inline:"0\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%#d" dep
}

rquery_rdeps_body() {
	setup_repo

	# %rn: reverse dep name
	atf_check \
		-o inline:"test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%rn" dep

	# %#r: reverse dep count
	atf_check \
		-o inline:"1\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%#r" dep

	# test has no rdeps
	atf_check \
		-o inline:"0\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%#r" test
}

rquery_options_body() {
	setup_repo

	# %Ok %Ov: option key and value
	atf_check \
		-o match:"OPT1" \
		-o match:"OPT2" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%Ok %Ov" test

	# %#O: option count
	atf_check \
		-o inline:"2\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%#O" test

	# dep has no options
	atf_check \
		-o inline:"0\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%#O" dep
}

rquery_categories_body() {
	setup_repo

	# %Cn: category names
	atf_check \
		-o match:"devel" \
		-o match:"test" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%Cn" test

	# %#C: category count
	atf_check \
		-o inline:"2\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%#C" test

	atf_check \
		-o inline:"1\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%#C" dep
}

rquery_size_body() {
	setup_repo

	# %sh: human-readable size
	atf_check \
		-o match:"0" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%sh" test

	# %sb: raw bytes
	atf_check \
		-o inline:"0\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%sb" test
}

rquery_eval_body() {
	setup_repo

	# -e: evaluation - match by name
	atf_check \
		-o inline:"test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e '%n == "test"' "%n"

	# No match
	atf_check \
		-o empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e '%n == "nosuch"' "%n"

	# Not equal
	atf_check \
		-o inline:"dep\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e '%n != "test"' "%n"

	# Has deps
	atf_check \
		-o inline:"test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e "%#d > 0" "%n"

	# Has no deps
	atf_check \
		-o inline:"dep\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e "%#d == 0" "%n"
}

rquery_eval_complex_body() {
	setup_repo

	# AND: has deps AND has options
	atf_check \
		-o inline:"test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e "%#d > 0 && %#O > 0" "%n"

	# OR: has deps OR name is dep
	atf_check \
		-o match:"test" \
		-o match:"dep" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e '%#d > 0 || %n == "dep"' "%n"

	# No results with impossible condition
	atf_check \
		-o empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e "%#d > 0 && %#O == 0" "%n"

	# Eval with explicit pattern
	atf_check \
		-o inline:"test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e '%n == "test"' "%n" test

	# Eval with pattern that does not match
	atf_check \
		-o empty \
		-s exit:1 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U -e '%n == "test"' "%n" nosuch
}

rquery_glob_body() {
	setup_repo

	# -g: glob match
	atf_check \
		-o inline:"test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -Ug "%n" 'tes*'

	# Glob matching all
	atf_check \
		-o match:"test" \
		-o match:"dep" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -Ug "%n" '*'

	# Glob no match
	atf_check \
		-o empty \
		-s exit:1 \
		pkg -o REPOS_DIR="${RDIR}" rquery -Ug "%n" 'zzz*'
}

rquery_regex_body() {
	setup_repo

	# -x: regex match
	atf_check \
		-o inline:"test\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -Ux "%n" '^tes'

	# Regex match all
	atf_check \
		-o match:"test" \
		-o match:"dep" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -Ux "%n" '.'

	# Regex no match
	atf_check \
		-o empty \
		-s exit:1 \
		pkg -o REPOS_DIR="${RDIR}" rquery -Ux "%n" '^zzz'
}

rquery_no_repo_body() {
	# No repo configured
	export REPOS_DIR=/nonexistent
	atf_check \
		-e match:"No active remote repositories" \
		-s exit:3 \
		pkg -C '' -R '' rquery -a "%n"
}

rquery_not_found_body() {
	setup_repo

	# Package not found
	atf_check \
		-o empty \
		-s exit:1 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%n" nosuchpkg
}

rquery_multiple_pkgs_body() {
	setup_repo

	# Query two packages at once
	atf_check \
		-o match:"dep" \
		-o match:"test" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%n" dep test

	# Mixed format: name-version with comment
	atf_check \
		-o match:"dep-1.0: a test" \
		-o match:"test-2.5: a test" \
		-s exit:0 \
		pkg -o REPOS_DIR="${RDIR}" rquery -U "%n-%v: %c" dep test
}

rquery_multi_repo_body() {
	# Create two separate repos with different packages
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "alpha" "alpha" "1.0" "/usr/local"
	atf_check pkg create -o ${TMPDIR}/repoA -M alpha.ucl

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "beta" "beta" "2.0" "/usr/local"
	atf_check pkg create -o ${TMPDIR}/repoB -M beta.ucl

	atf_check -o ignore pkg repo ${TMPDIR}/repoA
	atf_check -o ignore pkg repo ${TMPDIR}/repoB

	mkdir -p ${TMPDIR}/reposconf
	cat << EOF > ${TMPDIR}/reposconf/multi.conf
repoA: {
    url: file://${TMPDIR}/repoA,
    enabled: true
}
repoB: {
    url: file://${TMPDIR}/repoB,
    enabled: true
}
EOF

	atf_check -o ignore \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		update

	# Both repos queried by default
	atf_check \
		-o match:"alpha" \
		-o match:"beta" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		rquery -Ua "%n"

	# -r: restrict to repoA
	atf_check \
		-o inline:"alpha\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		rquery -Ur repoA -a "%n"

	# -r: restrict to repoB
	atf_check \
		-o inline:"beta\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		rquery -Ur repoB -a "%n"
}
