#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	fingerprint_ecc \
	fingerprint_rsa \
	fingerprint_rootdir

setup() {
	local _root=$1
	local _type=$2
	local _fingerprint
	local _typecmd

	case "$_type" in
	rsa)
		atf_skip_on Linux Test fails on Linux
		atf_check -o save:repo.pub -e ignore \
			pkg key --create repo.key
		_typecmd=""
		;;
	ecc)
		atf_skip_on Darwin Test fails on Darwin
		atf_skip_on Linux Test fails on Linux
		atf_check -o ignore -e ignore \
			openssl ecparam -genkey -name secp256k1 -out repo.key -outform DER
		chmod 0400 repo.key
		atf_check -o ignore -e ignore \
			openssl ec -in repo.key -pubout -out repo.pub -outform DER
		_typecmd='printf "%s\n%s\n" "TYPE" "ecdsa"'
		;;
	esac

	rm -rf ${TMPDIR}/keys || :
	mkdir -p ${_root}/${TMPDIR}/keys/trusted
	mkdir -p ${_root}/${TMPDIR}/keys/revoked
	_fingerprint=$(openssl dgst -sha256 -hex repo.pub | sed 's/^.* //')
	echo "function: sha256" > ${_root}/${TMPDIR}/keys/trusted/key
	echo "fingerprint: \"${_fingerprint}\"" >> ${_root}/${TMPDIR}/keys/trusted/key
	mkdir fakerepo

	cat >> sign.sh << EOF
#!/bin/sh
read -t 2 sum
[ -z "\$sum" ] && exit 1

$_typecmd
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

fingerprint_ecc_body() {
	setup "" "ecc"

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" update
}

fingerprint_rsa_body() {
	setup "" "rsa"

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" update
}

fingerprint_rootdir_body() {
	setup "${TMPDIR}/rootdir" "rsa"

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" -r "${TMPDIR}/rootdir" update
}
