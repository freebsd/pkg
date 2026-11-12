#! /usr/bin/env atf-sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 The FreeBSD Foundation
#
# This software was developed by Klara, Inc. under sponsorship from the
# FreeBSD Foundation and the Sovereign Tech Agency.
#

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	upgradable

upgradable_body() {
	# Make up some packages
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg a1 a 1.1
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg a2 a 1.2
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg b1 b 2.72
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg b2 b 3.14
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg c1 c 0.42
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg d1 d 23.19

	# Install a1, b1, c1 locally
	atf_check -o ignore pkg register -M a1.ucl
	atf_check -o ignore pkg register -M b1.ucl
	atf_check -o ignore pkg register -M c1.ucl

	# Create an enabled repository with just a1 (pkg refuses to
	# create an empty repository) so pkg won't complain about not
	# having any
	atf_check mkdir empty
	atf_check -o ignore pkg create -o empty -M ./a1.ucl
	atf_check -o match:'repository in empty' pkg repo empty
	atf_check mkdir conf
	atf_check -o save:conf/empty.conf \
		  echo "empty: { url: file:///$TMPDIR/empty, enabled: true }"
	export REPOS_DIR="$TMPDIR/conf"
	atf_check -o match:'Updating empty repo' \
		  pkg update

	# Create a disabled repository containing a2, b1, and d
	atf_check mkdir repo
	atf_check -o ignore pkg create -o test -M ./a2.ucl
	atf_check -o ignore pkg create -o test -M ./b1.ucl
	atf_check -o ignore pkg create -o test -M ./d1.ucl
	atf_check -o match:'repository in test' pkg repo test
	atf_check -o save:conf/test.conf \
		  echo "test: { url: file:///$TMPDIR/test, enabled: false }"
	atf_check -o match:'Updating test repo' \
		  pkg update -r test

	# The repository is disabled, so nothing is upgradable
	atf_check -s exit:1 -o ignore \
		  pkg upgradable -U

	# Try again with explicit repository
	atf_check -s exit:0 \
		  -o match:'a: 1\.1 -> 1\.2' \
		  -o not-match:'b:' \
		  -o not-match:'c:' \
		  -o not-match:'d:' \
		  pkg upgradable -r test -U

	# Quiet mode
	atf_check -s exit:0 \
		  -o empty \
		  pkg upgradable -r test -U -q

	# Now an explicit package
	atf_check -s exit:1 \
		  -o empty \
		  pkg upgradable -r test -U b

	# Now replace b1 with b2 in the repository, and switch it from
	# disabled to enabled
	atf_check rm test/b-2.72.pkg
	atf_check -o ignore pkg create -o test -M ./b2.ucl
	atf_check sleep 1 # make sure the mtime changes
	atf_check -o match:'repository in test' pkg repo test
	atf_check -o save:conf/test.conf \
		  echo "test: { url: file:///$TMPDIR/test, enabled: true }"

	# From here on out, we'll use the UK spelling...

	# With auto-update disabled, the result has not changed
	atf_check -s exit:1 \
		  pkg upgradeable -q -U b

	# With auto-update enabled, we should see the update happen,
	# and b should now be upgradable
	atf_check -s exit:0 \
		  -o match:'Updating test repo' \
		  -o match:'b: 2\.72 -> 3\.14' \
		  pkg upgradeable b

	# Now an explicit package; a is still upgradable but should
	# not be listed
	atf_check -s exit:0 \
		  -o not-match:'a:' \
		  -o match:'b: 2\.72 -> 3\.14' \
		  -o not-match:'c:' \
		  -o not-match:'d:' \
		  pkg upgradeable -r test -U b

	# Test again without explicit repository
	atf_check -s exit:0 \
		  -o match:'a: 1\.1 -> 1\.2' \
		  -o match:'b: 2\.72 -> 3\.14' \
		  -o not-match:'c:' \
		  -o not-match:'d:' \
		  pkg upgradeable -U
	atf_check -s exit:0 -o empty pkg upgradable -U -q
}
