#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic \
	message \
	daemon \
	upgrade \
	stop

stop_body() {
	# A post-install script exiting with 3 stops the operation after its
	# package, which is installed; the operation is a success.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "a" "a" "1"
	cat << EOF >> a.ucl
scripts: {
  post-install: <<EOS
	echo stopping here
	exit 3
EOS
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "b" "b" "1"
	cat << EOF >> b.ucl
deps: {
  a: {
    origin: "a",
    version: "1"
  }
}
EOF
	atf_check pkg create -M a.ucl
	atf_check pkg create -M b.ucl
	mkdir ${TMPDIR}/target
	atf_check -o match:"stopping here" \
	    -o match:"Stopping after a-1 as requested by its post-install script" \
	    -s exit:0 \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -fy ${TMPDIR}/b-1.pkg ${TMPDIR}/a-1.pkg
	atf_check -o inline:"a-1\n" \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target info -q

	# the same through pkg add
	atf_check -o match:"stopping here" \
	    -e match:"Stopping after .*a-1.pkg as requested by its post-install script" \
	    -s exit:0 \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target add -f ${TMPDIR}/a-1.pkg ${TMPDIR}/b-1.pkg
	atf_check -o inline:"a-1\n" \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target info -q

	# in any other phase status 3 is a plain failure
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "c" "c" "1"
	cat << EOF >> c.ucl
scripts: {
  pre-install: <<EOS
	exit 3
EOS
}
EOF
	atf_check pkg create -M c.ucl
	atf_check -s not-exit:0 \
	    -e match:"PRE-INSTALL script failed" \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/c-1.pkg
}

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
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

}

message_body() {
	# The message should be the last thing planned
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
scripts: {
  post-install: <<EOS
	echo this is post install1
	echo this is a message >&\${PKG_MSGFD}
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
		-o save:output \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -fy ${TMPDIR}/test-1.pkg

	# PKG_MSGFD message must appear after script stdout
	atf_check -o inline:"this is post install1\nthis is post install2\nthis is a message\n" \
		grep -E "post install|a message" output
}

daemon_body() {
	# We should not see the daemon's message
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
scripts: {
  post-install: <<EOS
	echo this is post install1
	echo this is a message >&\${PKG_MSGFD}
	(sleep 2; echo this is a daemon >&\${PKG_MSGFD}) < /dev/null > /dev/null 2>&1 &
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
		-o save:output \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -fy ${TMPDIR}/test-1.pkg

	# PKG_MSGFD message must appear after script stdout, daemon must not appear
	atf_check -o inline:"this is post install1\nthis is post install2\nthis is a message\n" \
		grep -E "post install|a message" output
	atf_check -s exit:1 grep "this is a daemon" output
}

upgrade_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
scripts: {
  post-install: <<EOS
if [ -n "\${PKG_UPGRADE+x}" ]; then
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
		-o ignore \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.pkg

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	cat << EOF >> test.ucl
scripts: {
  post-install: <<EOS
if [ -n "\${PKG_UPGRADE+x}" ]; then
	echo "upgrade:\${PKG_UPGRADE}">&\${PKG_MSGFD}
fi
EOS
}
EOF

	rm ${TMPDIR}/test-1.pkg
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
		pkg info -R -F ./test-2.pkg
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
		-o match:"upgrade:true" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -r ${TMPDIR}/target upgrade -y
}
