#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	vital \
	vital_dep_remove_message \
	user_vital

vital_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "prefix"
	cat << EOF >> test.ucl
vital = true;
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o match:"^vital" \
		-e empty \
		-s exit:0 \
		pkg info -R --raw-format ucl -F ${TMPDIR}/test-1.pkg

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.pkg

	atf_check \
		-o inline:"1\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target query "%V" test

	atf_check \
		-o inline:"The following package(s) are locked or vital and may not be removed:\n\n\ttest (vital)\n\n" \
		-e empty  \
		-s exit:7 \
		pkg -r ${TMPDIR}/target delete -qy test
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -r ${TMPDIR}/target delete -qyf test
}

vital_dep_remove_message_body()
{
	# vital_pkg depends on dep; removing dep should produce
	# an informative error mentioning the vital package.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "dep" "dep" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "vital_pkg" "vital_pkg" "1"
	cat << EOF >> vital_pkg.ucl
vital = true;
deps: {
	dep {
		origin: dep,
		version: "1"
	}
}
EOF

	atf_check -o ignore pkg register -M dep.ucl
	atf_check -o ignore pkg register -M vital_pkg.ucl

	# Removing dep should fail with a message about vital_pkg
	atf_check \
		-e match:"Cannot remove dep: required by vital package vital_pkg" \
		-s exit:1 \
		pkg delete -qy dep

	# dep should still be installed
	atf_check -s exit:0 pkg info -e dep

	# vital_pkg should still be installed
	atf_check -s exit:0 pkg info -e vital_pkg

	# Force remove should still work
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg delete -qyf dep
}

user_vital_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "prefix"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.pkg

	# the package is not vital, the manifest does not set the flag
	atf_check \
		-o inline:"0\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target query "%V" test

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target set -y -v 1 test

	atf_check \
		-o inline:"1\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target query "%V" test

	# the user flag is local state and must survive a reinstall
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.pkg

	atf_check \
		-o inline:"1\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target query "%V" test

	atf_check \
		-o inline:"The following package(s) are locked or vital and may not be removed:\n\n\ttest (vital)\n\n" \
		-e empty \
		-s exit:7 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target delete -qy test

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target set -y -v 0 test

	atf_check \
		-o inline:"0\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target query "%V" test

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target delete -qy test
}
