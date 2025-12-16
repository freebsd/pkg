#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	vital

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
