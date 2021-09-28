#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	fingerprint \
	fingerprint_rootdir

setup() {
	local _root=$1
        atf_skip_on Darwin Test fails on Darwin
        atf_skip_on Linux Test fails on Linux

	atf_check -o ignore -e ignore \
		openssl genrsa -out repo.key 2048
	rm -rf ${TMPDIR}/keys || :
	mkdir -p ${_root}/${TMPDIR}/keys/trusted
	mkdir -p ${_root}/${TMPDIR}/keys/revoked
	chmod 0400 repo.key
	atf_check -o ignore -e ignore \
		openssl rsa -in repo.key -out repo.pub -pubout
	echo "function: sha256" > ${_root}/${TMPDIR}/keys/trusted/key
	echo -n "fingerprint: " >> ${_root}/${TMPDIR}/keys/trusted/key
	openssl dgst -sha256 -hex repo.pub | sed 's/^.* //' >> ${_root}/${TMPDIR}/keys/trusted/key
	echo "" >> ${_root}/${TMPDIR}/keys/trusted/key
	mkdir fakerepo

	cat >> sign.sh << EOF
#!/bin/sh
read -t 2 sum
[ -z "\$sum" ] && exit 1
echo SIGNATURE
echo -n \$sum | /usr/bin/openssl dgst -sign repo.key -sha256 -binary
echo
echo CERT
cat repo.pub
echo END
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	atf_check -o ignore -e ignore \
		pkg create -M test.ucl -o fakerepo
	atf_check -o ignore \
		pkg repo fakerepo signing_command: sh sign.sh

	cat >> repo.conf << EOF
local: {
	url: file:///${TMPDIR}/fakerepo
	enabled: true
	signature_type: FINGERPRINTS
	fingerprints: ${TMPDIR}/keys
}
EOF
}

fingerprint_body() {
	setup ""

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" update
}

fingerprint_rootdir_body() {
	setup "${TMPDIR}/rootdir"

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" -r "${TMPDIR}/rootdir" update
}
