#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic \
	dirs \
	missing

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
	if [ "${OS}" = "FreeBSD" -o ${OS} = "Darwin" ]; then
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

# A package which is no longer available in the repositories can neither be
# reinstalled nor upgraded; one which was never installed from a repository is
# not expected to be found in one and is not reported (#831, #1096).
missing_body() {
	pkgargs="-o REPOS_DIR=${TMPDIR}/reposconf -o PKG_CACHEDIR=${TMPDIR}"

	mkdir repo reposconf
	cat > reposconf/repos.conf <<__EOF__
test: {
	url: file://${TMPDIR}/repo,
	enabled: true
}
__EOF__

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "t1" "t1" "1"
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "t2" "t2" "1"
	atf_check -o ignore pkg create -M t1.ucl -o repo
	atf_check -o ignore pkg create -M t2.ucl -o repo
	atf_check -o ignore pkg repo repo

	atf_check -o ignore pkg ${pkgargs} update -f
	atf_check -o ignore pkg ${pkgargs} install -y t1 t2

	# installed from a file, this one never came from a repository
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "local" "local" "1"
	atf_check -o ignore pkg create -M local.ucl
	atf_check -o ignore pkg ${pkgargs} add ./local-1.pkg

	# all of them are available: nothing to report
	atf_check -o ignore -s exit:0 pkg ${pkgargs} check -R

	# t2 disappears from the repository
	rm repo/t2-1.pkg
	atf_check -o ignore pkg repo repo
	atf_check -o ignore pkg ${pkgargs} update -f

	atf_check -o match:"t2-1 is not available in the repositories" \
	    -s exit:1 pkg ${pkgargs} check -R
	atf_check -o not-match:"local-1" -s exit:1 pkg ${pkgargs} check -R
	atf_check -o inline:"t2\n" -s exit:1 pkg ${pkgargs} check -Rq
}
