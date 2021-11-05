#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	fetch_missing \
	fetch_missing_dep

test_setup()
{
	# Do a local config to avoid permissions-on-system-db errors.
        cat > ${TMPDIR}/pkg.conf << EOF
PKG_CACHEDIR=${TMPDIR}/cache
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[
	${TMPDIR}/reposconf
]
repositories: {
        local: { url : file://${TMPDIR} }
}
EOF
	# Create one package so we at least have a repo.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ${TMPDIR}/test test 1 /usr/local
	cat << EOF >> ${TMPDIR}/test.ucl
deps: {
	b: {
		origin: "wedontcare",
		version: "1"
	}
}
EOF
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C "${TMPDIR}/pkg.conf" create -o ${TMPDIR} -M ${TMPDIR}/test.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C "${TMPDIR}/pkg.conf" repo ${TMPDIR}

	mkdir -p ${TMPDIR}/reposconf
	cat << EOF > ${TMPDIR}/reposconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF
}

fetch_missing_body()
{
	test_setup
    
	atf_check \
		-o ignore \
		-e not-empty \
		-s not-exit:0 \
		pkg -C "${TMPDIR}/pkg.conf" fetch -r local -y missing
}

fetch_missing_dep_body()
{
	test_setup

	atf_check \
		-o ignore \
		-e match:"pkg: test has a missing dependency: b$" \
		-s not-exit:0 \
		pkg -C "${TMPDIR}/pkg.conf" fetch -r local -d -y test
}

