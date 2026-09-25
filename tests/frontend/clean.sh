#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic \
	clean_all_no_repo_db \
	clean_no_repo_db \
	keep_installed_archive \
	keep_installed_archive_no_cksum

basic_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

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

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl
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
		-o ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" update

	mkdir cache
	mv test-* cache
	atf_check \
		-e empty \
		-o match:"test-.\.pkg" \
		-s exit:0 \
		pkg -C /dev/null -o PKG_CACHEDIR=${TMPDIR}/cache -o REPOS_DIR="${TMPDIR}/reposconf" clean -n
}

clean_all_no_repo_db_body() {
	# pkg clean -a should work even without any repo database
	mkdir -p reposconf cache
	cat > reposconf/repo.conf << EOF
testrepo: {
    url: "file:///nonexistent",
    enabled: true
}
EOF
	echo "fake" > cache/test-1~abc123.pkg

	atf_check \
		-o match:"test-1" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR=${TMPDIR}/cache clean -an
}

clean_no_repo_db_body() {
	# pkg clean (without -a) should treat all cached files as obsolete
	# when no repo database exists, instead of erroring out
	mkdir -p reposconf cache
	cat > reposconf/repo.conf << EOF
testrepo: {
    url: "file:///nonexistent",
    enabled: true
}
EOF
	echo "fake" > cache/test-1~abc123.pkg

	atf_check \
		-o match:"test-1" \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR=${TMPDIR}/cache clean -n
}

keep_installed_archive_body() {
	# An archive fetched from a repository has its checksum recorded at
	# install time.  pkg clean must keep that archive as long as the
	# package is installed, even when the version is not available in
	# any repository anymore.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}/root"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "other" "other" "1" "${TMPDIR}/root"

	atf_check -o ignore -e empty -s exit:0 pkg create -M test.ucl
	atf_check -o ignore -e empty -s exit:0 pkg create -M other.ucl

	mkdir repo
	mv test-1.pkg other-1.pkg repo/
	atf_check -o ignore -e empty -s exit:0 env PKG_REPO_HASH=1 pkg repo repo

	# the checksum embedded in the repository file name
	hash10=$(basename repo/Hashed/test-1~*.pkg)
	hash10=${hash10#*~}
	hash10=${hash10%.pkg}
	atf_check test -f "repo/Hashed/test-1~${hash10}.pkg"

	mkdir -p cache root reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR}/repo,
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}/cache" update
	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}/cache" install -y test

	# the archive of the installed package, under its own name and under
	# an unrelated one: the checksum is what identifies the archive
	touch "cache/test-1~${hash10}.pkg" "cache/renamed-9~${hash10}.pkg"

	# Drop test from the repository, keeping other so the repo stays valid.
	# A file rebuilt within the same second keeps the same mtime, and the
	# file:// fetcher would then answer EPKG_UPTODATE and leave the cached
	# catalogue unchanged, so make sure the rebuilt catalogue is at least
	# one second newer.
	rm -f repo/Hashed/test-1~*.pkg
	sleep 1
	atf_check -o ignore -e empty -s exit:0 env PKG_REPO_HASH=1 pkg repo repo
	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}/cache" update

	atf_check -o match:"Nothing to do" -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}/cache" clean -n

	atf_check -o ignore -e empty -s exit:0 pkg delete -y test

	atf_check -o match:"test-1~" -o match:"renamed-9~" -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}/cache" clean -n
}

keep_installed_archive_no_cksum_body() {
	# A package registered from a local manifest has no recorded archive
	# checksum: its cached archive is matched on <name>-<version>.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}/root"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "other" "other" "1" "${TMPDIR}/root"

	atf_check -o ignore -e empty -s exit:0 pkg create -M other.ucl
	mkdir repo
	mv other-1.pkg repo/
	atf_check -o ignore -e empty -s exit:0 pkg repo repo

	mkdir -p cache root reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR}/repo,
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}/cache" update
	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" register -M test.ucl

	touch cache/test-1~deadbeef00.pkg

	atf_check -o match:"Nothing to do" -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}/cache" clean -n

	atf_check -o ignore -e empty -s exit:0 pkg delete -y test

	atf_check -o match:"test-1~deadbeef00\.pkg" -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}/cache" clean -n
}
