#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	update_error \
	file_url \
	update_disabled_repo \
	update_all_disabled_repos \
	update_override_disabled_repo \
	update_nonexistent_repo \
	failed_update_keeps_catalog

update_error_body() {

	mkdir repos
	mkdir empty
	cat > repos/test.conf << EOF
test: {
  url: "file://empty/",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid URL: 'file://empty//meta.conf'" \
		-s exit:1 \
		pkg -R repos update
}

file_url_body() {
	mkdir repos
	touch meta.conf
	here=$(pwd)


#
# test file:/empty/, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:/empty/",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid URL: 'file:/empty//meta.conf" \
		-s exit:1 \
		pkg -R repos update

#
# test file://here, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://here",
}
EOF
	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid URL: 'file://here/meta.conf'" \
		-s exit:1 \
		pkg -R repos update


#
# test file://here//path, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://here/${here}",
}
EOF
	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file:////path, which is valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:///${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file:///path, which is valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file://path, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:/${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid URL: 'file:/${here}/meta.conf'" \
		-s exit:1 \
		pkg -R repos update


#
# test file://localhost/path, which is a valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://localhost${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

}

# Verify that "pkg update -r <repo>" works on a disabled repository,
# as documented in the man page ("irrespective of the configured active
# status").
update_disabled_repo_body()
{
	# Create a package and repo
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check pkg create -M test.ucl
	atf_check -o ignore pkg repo .

	# Configure two repos: one enabled, one disabled
	mkdir repos
	cat <<EOF > repos/enabled.conf
enabled_repo: {
	url: "file://${TMPDIR}",
	enabled: yes
}
EOF
	cat <<EOF > repos/disabled.conf
disabled_repo: {
	url: "file://${TMPDIR}",
	enabled: no
}
EOF

	atf_check \
		-o match:"Updating enabled_repo" \
		-o not-match:"disabled_repo" \
		-s exit:0 \
		pkg -R repos update

	atf_check \
		-o match:"Updating disabled_repo" \
		-o not-match:"enabled_repo" \
		-s exit:0 \
		pkg -R repos update -r disabled_repo

	atf_check \
		-o match:"No repositories are enabled" \
		-e match:"WARNING: repository 'nonexistent' does not exist" \
		-s exit:1 \
		pkg -R repos update -r nonexistent
}

# Verify that "pkg update -r <repo>" works when ALL repositories are
# disabled (not just when some are enabled and some are not).
update_all_disabled_repos_body()
{
	# Create a package and repo
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check pkg create -M test.ucl
	atf_check -o ignore pkg repo .

	# Configure only disabled repos
	mkdir repos
	cat <<EOF > repos/repo1.conf
repo1: {
	url: "file://${TMPDIR}",
	enabled: no
}
EOF
	cat <<EOF > repos/repo2.conf
repo2: {
	url: "file://${TMPDIR}",
	enabled: no
}
EOF

	# Without -r, no repos should be updated
	atf_check \
		-o match:"No repositories are enabled" \
		-s exit:1 \
		pkg -R repos update

	# With -r, the named disabled repo should be updated
	atf_check \
		-o match:"Updating repo1" \
		-o not-match:"repo2" \
		-s exit:0 \
		pkg -R repos update -r repo1
}

# Verify that "pkg update -r <repo>" works when a repository is initially
# enabled in one config file and then overridden to disabled in another.
# This is the scenario from GitHub issue #2643.
update_override_disabled_repo_body()
{
	# Create a package and repo
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check pkg create -M test.ucl
	atf_check -o ignore pkg repo .

	# First config file defines the repo as enabled
	mkdir repos
	cat <<EOF > repos/base.conf
myrepo: {
	url: "file://${TMPDIR}",
	enabled: yes
}
EOF
	# Second config file overrides the repo to disabled
	cat <<EOF > repos/override.conf
myrepo: {
	enabled: no
}
EOF

	# Without -r, no repos should be updated
	atf_check \
		-o match:"No repositories are enabled" \
		-s exit:1 \
		pkg -R repos update

	# With -r, the overridden-disabled repo should still be updated
	atf_check \
		-o match:"Updating myrepo" \
		-s exit:0 \
		pkg -R repos update -r myrepo
}

update_nonexistent_repo_body()
{
	# Specifying a nonexistent repo with -r should warn
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check pkg create -M test.ucl
	atf_check -o ignore pkg repo .

	mkdir repos
	cat <<EOF > repos/real.conf
real: {
	url: "file://${TMPDIR}",
	enabled: true
}
EOF

	atf_check \
		-o match:"Updating real" \
		-s exit:0 \
		pkg -R repos update -r real

	# Update with a mix of real and nonexistent repos
	atf_check \
		-e match:"WARNING: repository 'bogus' does not exist" \
		-o match:"Updating real" \
		-s exit:0 \
		pkg -R repos update -r real -r bogus

	# Update with only a nonexistent repo
	atf_check \
		-o match:"No repositories are enabled" \
		-e match:"WARNING: repository 'bogus' does not exist" \
		-s exit:1 \
		pkg -R repos update -r bogus
}

# A failed update must not throw away the catalogue which has previously
# been fetched: the repository database is only removed when it cannot be
# opened anymore, so a failed or interrupted update still leaves a usable
# catalogue behind.
failed_update_keeps_catalog_body()
{
	# Create a package and a repository holding it
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check pkg create -M test.ucl
	mkdir repo
	mv test-1.pkg repo/
	atf_check -o ignore pkg repo repo

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
PKG_CACHEDIR=${TMPDIR}/cache
REPOS_DIR=[]
repositories: {
	local: { url: file://${TMPDIR}/repo }
}
EOF

	# Fetch the catalogue
	atf_check -o ignore pkg -C ./pkg.conf update

	# Make the repository unreachable, the update cannot complete
	mkdir repo.hide
	mv repo/data.pkg repo/packagesite.pkg repo/meta.conf repo.hide/
	atf_check \
		-o match:"Unable to update repository local" \
		-e ignore \
		-s exit:1 \
		pkg -C ./pkg.conf update

	# The previously fetched catalogue has been kept and is still usable
	atf_check -o match:"test 1" pkg -C ./pkg.conf rquery -U "%n %v"

	# and updates work again as soon as the repository is back
	mv repo.hide/* repo/
	atf_check -o ignore pkg -C ./pkg.conf update
}
