#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic \
	message \
	upgrade

basic_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
scripts: {
  post-install: <<EOS
	echo this is post install1
	echo this is post install2
EOS
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o inline:"this is post install1\nthis is post install2\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

}

message_body() {
	# The message should be the last thing planned
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
scripts: {
  post-install: <<EOS
	echo this is post install1
	echo this is a message >\&\${PKG_MSGFD}
	echo this is post install2
EOS
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o inline:"this is post install1\nthis is post install2\nthis is a message\n" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

}

upgrade_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
scripts: {
  post-install: <<EOS
if [ -n "\${PKG_UPGRADE+x}" ]; then
	echo "upgrade:\${PKG_UPGRADE}"
	echo "upgrade:\${PKG_UPGRADE}">&\${PKG_MSGFD}
fi
EOS
}
EOF


	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl
	mkdir -p ${TMPDIR}/target
	atf_check \
		-e empty \
		-o empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	cat << EOF >> test.ucl
scripts: {
  post-install: <<EOS
if [ -n "\${PKG_UPGRADE+x}" ]; then
	echo "upgrade:\${PKG_UPGRADE}">\&\${PKG_MSGFD}
	echo this is a message >\&\${PKG_MSGFD}
fi
EOS
}
EOF

	rm ${TMPDIR}/test-1.txz
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl
	mkdir -p ${TMPDIR}/target
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg info -R -F ./test-2.txz
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo .
	mkdir reposconf
	cat <<EOF >> reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF
	atf_check \
		-e empty \
		-o match:"upgrade:1" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -r ${TMPDIR}/target upgrade -y
}
