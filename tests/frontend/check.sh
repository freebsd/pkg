#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic \
	dirs

basic_body() {
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	user=$(id -un)
	if [ "${OS}" = "FreeBSD" ]; then
	    group=$(stat -f "%Sg" .)
	else
	    group=$(id -gn)
	fi
	cat <<__EOF__ >> test.ucl
files: {
    ${TMPDIR}/a: {
		 uname: "${user}"
		 gname: "${group}"
		 perm: 0644
    },
}
__EOF__

	echo a > a
	atf_check pkg create -M test.ucl
	atf_check mkdir -p target
	atf_check pkg -o REPOS_DIR=/dev/null -r target install -qfy ${TMPDIR}/test-1.pkg
	atf_check pkg -r target check -q
	atf_check -s exit:0 pkg -r target check -mq
	echo b > ${TMPDIR}/target/${TMPDIR}/a
	touch -r ./a ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s not-exit:0 -e inline:"test-1: checksum mismatch for ${TMPDIR}/a\n" \
		  pkg -r target check -q
	atf_check -s exit:0 pkg -r target check -mq
	touch -t 197001010000.01 ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s exit:1 -e match:"\[mtime\]" pkg -r target check -mq
	touch -r ./a ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s exit:0 pkg -r target check -mq
	ln -sf b ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s exit:1 -e match:"\[symlink\]" -e match:"\[perm\]" pkg -r target check -mq
	chmod -h 0644 ${TMPDIR}/target/${TMPDIR}/a
	touch -hr ./a ${TMPDIR}/target/${TMPDIR}/a
	# linux mangles perms on symlinks
	if [ "${OS}" = "FreeBSD" ]; then
	    atf_check -s exit:1 -e match:"\[symlink\]" -e not-match:"\[perm\]" -e not-match:"\[mtime\]" pkg -r target check -mq
	else
	    atf_check -s exit:1 -e match:"\[symlink\]" -e match:"\[perm\]" -e not-match:"\[mtime\]" pkg -r target check -mq
	fi
	rm ${TMPDIR}/target/${TMPDIR}/a
	mkdir ${TMPDIR}/target/${TMPDIR}/a
	touch -r a ${TMPDIR}/target/${TMPDIR}/a
	chmod 0644 ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s exit:1 -e match:"\[type\]" pkg -r target check -mq
}

dirs_body() {
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	user=$(id -un)
	if [ "${OS}" = "FreeBSD" ]; then
	    group=$(stat -f "%Sg" .)
	else
	    group=$(id -gn)
	fi
	cat <<__EOF__ >> test.ucl
directories: {
    ${TMPDIR}/a: {
		 uname: "${user}"
		 gname: "${group}"
		 perm: 0644
    },
}
__EOF__

	mkdir a
	atf_check pkg create -M test.ucl
	atf_check mkdir -p target
	atf_check pkg -o REPOS_DIR=/dev/null -r target install -qfy ${TMPDIR}/test-1.pkg
	atf_check pkg -r target check -mq
	# mtime is not checked for directories
	touch -t 197001010000.01 ${TMPDIR}/target/${TMPDIR}/a
	atf_check pkg -r target check -mq
	chmod 0600 ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s exit:1 -e match:"\[perm\]" pkg -r target check -mq
	chmod 0644 ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s exit:0 pkg -r target check -mq
	rmdir ${TMPDIR}/target/${TMPDIR}/a
	touch ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s exit:1 -e match:"\[type\]" pkg -r target check -mq
}
