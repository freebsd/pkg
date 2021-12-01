#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

TEST_ROOT=${TMPDIR}
CONF=${TEST_ROOT}/conf
REPOS=${TEST_ROOT}/repos
REPO=${TEST_ROOT}/repo
DB=${TEST_ROOT}/db
CACHE=${TEST_ROOT}/cache
WORK=${TEST_ROOT}/work

PKG_DBDIR=${DB}

tests_init \
	fetch_dep_success \
	fetch_missing \
	fetch_missing_dep \
	fetch_missing_file \
	fetch_missing_dep_file

test_setup()
{
	variant=$1
	atf_check rm -rf ${TEST_ROOT}/*
	atf_check mkdir -p ${CONF} ${REPOS} ${REPO} ${DB} ${CACHE} ${WORK}
	# Do a local config to avoid permissions-on-system-db errors.
        if ! cat > ${CONF}/pkg.conf << EOF
PKG_CACHEDIR=${CACHE}
PKG_DBDIR=${DB}
REPOS_DIR=[
	${REPOS}
]
repositories: {
        test: { url : file://${REPO} }
}
EOF
	then
	    atf_fail
	fi
	# Create two packages so we at least have a repo.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ${WORK}/test test 1 /usr/local
	if ! cat << EOF >> ${WORK}/test.ucl
deps: {
	b: {
		origin: "b",
		version: "1"
	}
}
EOF
	then
	    atf_fail
	fi
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C "${CONF}/pkg.conf" create -o ${REPO} -M ${WORK}/test.ucl
	atf_check rm -f ${WORK}/*

	if [ "${variant}" != "missing-pkg" ]; then
		atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ${WORK}/b b 1 /usr/local
		atf_check \
			-o ignore \
			-e empty \
			-s exit:0 \
			pkg -C "${CONF}/pkg.conf" create -o ${REPO} -M ${WORK}/b.ucl
		atf_check rm -f ${WORK}/*
	fi

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C "${CONF}/pkg.conf" repo ${REPO}

	if [ "${variant}" = "missing-file" ]; then
		atf_check rm -f ${REPO}/test-1.pkg
	fi
	if [ "${variant}" = "missing-dep-file" ]; then
		atf_check rm -f ${REPO}/b-1.pkg
	fi

	cat << EOF > ${REPOS}/test.conf
test: {
	url: file:///${REPO},
	enabled: true
}
EOF
	atf_check \
		-o ignore \
		pkg -C "${CONF}/pkg.conf" update
	pkg -C "${CONF}/pkg.conf" search .
}

fetch_dep_success_body()
{
	test_setup

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C "${CONF}/pkg.conf" fetch -U -r test -y -d test
}

fetch_missing_body()
{
	test_setup

	atf_check \
		-o ignore \
		-e not-empty \
		-s not-exit:0 \
		pkg -C "${CONF}/pkg.conf" fetch -r test -y missing
}

fetch_missing_dep_body()
{
	test_setup missing-pkg

	atf_check \
		-o ignore \
		-e match:"pkg: test has a missing dependency: b$" \
		-s not-exit:0 \
		pkg -C "${CONF}/pkg.conf" fetch -r test -d -y test
}

fetch_missing_file_body()
{
	test_setup missing-file

	atf_check \
		-o ignore \
		-e match:"cached package test-1: file://.*/test-1.pkg is missing from repo" \
		-s not-exit:0 \
		pkg -C "${CONF}/pkg.conf" fetch -r test -d -y test
}

fetch_missing_dep_file_body()
{
	test_setup missing-dep-file

	atf_check \
		-o ignore \
		-e match:"cached package b-1: file://.*/b-1.pkg is missing from repo" \
		-s not-exit:0 \
		pkg -C "${CONF}/pkg.conf" fetch -r test -d -y test
}

