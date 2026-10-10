#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	provides_order \
	provides_mutual \
	provides_scc_contiguous \
	shlibs_order \
	shlibs_mutual

setup_repo()
{
	mkdir reposconf
	cat << EOF >> reposconf/repo.conf
local1: {
	url: file://${TMPDIR}
	enabled: true
}
EOF
	atf_check -o ignore -e empty -s exit:0 pkg repo .
}

# Check that the packages were installed in the given order.
check_order()
{
	atf_check -o inline:"$1\n$2\n" sh -c \
	    "grep Installing ${TMPDIR}/out | sed -e 's/.*Installing //' -e 's/-1.0.*//'"
}

# A package providing a named provide must be installed before the package
# requiring it, even when its name sorts after the name of the consumer.  The
# packages are deliberately named so that the alphabetical order differs from
# the dependency order.
provides_order_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg zzz zzz 1.0
	cat << EOF >> zzz.ucl
provides: [libz-1]
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg aaa aaa 1.0
	cat << EOF >> aaa.ucl
requires: [libz-1]
EOF
	for p in zzz aaa; do
		atf_check -o ignore -e empty -s exit:0 pkg create -M ./${p}.ucl
	done
	setup_repo

	atf_check -e ignore -s exit:0 -o save:${TMPDIR}/out \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y aaa

	check_order zzz aaa
}

# Two packages requiring each other's provides form a cycle in the install
# graph.  Such a cycle cannot be broken by splitting upgrade jobs, so the
# scheduler must tolerate it instead of failing with "failed to break job
# scheduling cycle".
provides_mutual_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg aaa aaa 1.0
	cat << EOF >> aaa.ucl
provides: [liba-1]
requires: [libz-1]
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg zzz zzz 1.0
	cat << EOF >> zzz.ucl
provides: [libz-1]
requires: [liba-1]
EOF
	for p in zzz aaa; do
		atf_check -o ignore -e empty -s exit:0 pkg create -M ./${p}.ucl
	done
	setup_repo

	atf_check -e ignore -s exit:0 -o ignore \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y aaa
}

# Same as provides_order but using shared libraries instead of named
# provides.  libtest2fbsd.so.1 requires libfoo.so.1, which the package zzz
# provides.
shlibs_order_body()
{
	atf_check cp ${RESOURCEDIR}/libfoo.so.1 ${TMPDIR}/libfoo.so.1
	atf_check cp ${RESOURCEDIR}/libtest2fbsd.so.1 ${TMPDIR}/libtest2fbsd.so.1

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg zzz zzz 1.0 /
	cat << EOF >> zzz.ucl
files: {
	${TMPDIR}/libfoo.so.1: "",
}
EOF
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg aaa aaa 1.0 /
	cat << EOF >> aaa.ucl
files: {
	${TMPDIR}/libtest2fbsd.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 pkg create -M ./zzz.ucl
	atf_check -o ignore -e empty -s exit:0 pkg create -M ./aaa.ucl
	setup_repo

	atf_check -e ignore -s exit:0 -o save:${TMPDIR}/out \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y aaa

	check_order zzz aaa
}

# Same as provides_mutual but using shared libraries instead of named
# provides.  FreeBSD-runtime and FreeBSD-libarchive look like this on a
# pkgbase system, so the scheduler must tolerate such a cycle.
shlibs_mutual_body()
{
	atf_skip_on Darwin Not sure how to test this there
	atf_skip_on Linux Not sure how to test this there

	atf_check touch empty.c
	atf_check cc -shared -Wl,-soname=liba.so.1 empty.c -o liba.so.1
	atf_check ln -s liba.so.1 liba.so
	atf_check cc -shared -Wl,-soname=libz.so.1 empty.c -o libz.so.1 -la -L.
	atf_check ln -s libz.so.1 libz.so
	atf_check cc -shared -Wl,-soname=liba.so.1 empty.c -o liba.so.1 -lz -L.

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg aaa aaa 1.0 /
	cat << EOF >> aaa.ucl
files: {
	${TMPDIR}/liba.so.1: "",
}
EOF
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg zzz zzz 1.0 /
	cat << EOF >> zzz.ucl
files: {
	${TMPDIR}/libz.so.1: "",
}
EOF
	atf_check -o ignore -e empty -s exit:0 pkg create -M ./aaa.ucl
	atf_check -o ignore -e empty -s exit:0 pkg create -M ./zzz.ucl
	setup_repo

	atf_check -e ignore -s exit:0 -o ignore \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y aaa
}

# aaa and ccc require each other's provides and form a cycle, bbb requires only
# aaa's provide.  The jobs of the cycle must be scheduled as one contiguous
# block before bbb, and bbb must never be interleaved between aaa and ccc.
provides_scc_contiguous_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg aaa aaa 1.0
	cat << EOF >> aaa.ucl
provides: [libb-1]
requires: [liba-1]
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bbb bbb 1.0
	cat << EOF >> bbb.ucl
requires: [libb-1]
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ccc ccc 1.0
	cat << EOF >> ccc.ucl
provides: [liba-1]
requires: [libb-1]
EOF
	for p in aaa bbb ccc; do
		atf_check -o ignore -e empty -s exit:0 pkg create -M ./${p}.ucl
	done
	setup_repo

	atf_check -e ignore -s exit:0 -o save:${TMPDIR}/out \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y bbb

	atf_check -o inline:"aaa\nccc\nbbb\n" sh -c \
	    "grep Installing ${TMPDIR}/out | sed -e 's/.*Installing //' -e 's/-1.0.*//'"
}
