#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	fingerprint_ecc \
	fingerprint_mldsa \
	fingerprint_rsa \
	fingerprint_rootdir

setup() {
	local _root=$1
	local _type=$2
	local _fingerprint
	local _typecmd
	local _signcmd
	local _repoargs

	_signcmd=""
	_repoargs="fakerepo signing_command: sh sign.sh"

	case "$_type" in
	rsa)
		atf_skip_on Linux Test fails on Linux
		atf_check -o save:repo.pub -e ignore \
			pkg key --create repo.key
		keyform=""
		_typecmd=""
		;;
	ecc)
		atf_skip_on Linux Test fails on Linux
		atf_check -o ignore -e ignore \
			openssl ecparam -genkey -name secp256k1 -out repo.key -outform DER
		chmod 0400 repo.key
		atf_check -o ignore -e ignore \
			openssl ec -inform DER -in repo.key -pubout -out repo.pub -outform DER
		keyform="-keyform DER"
		_typecmd='printf "%s\n%s\n" "TYPE" "ecdsa"'
		;;
	mldsa)
		atf_require_signer mldsa
		atf_check -o save:repo.pub -e ignore \
			pkg key --create -t mldsa repo.key
		keyform=""
		_typecmd='printf "%s\n%s\n" "TYPE" "mldsa"'
		_signcmd='pkg key --sign -t mldsa repo.key'
		# The signer type selects the digest passed to sign.sh.
		_repoargs="--sign-type mldsa --sign-cmd 'sh sign.sh' fakerepo"
		;;
	esac
	: ${_signcmd:="openssl dgst $keyform -sign repo.key -sha256 -binary"}

	rm -rf ${TMPDIR}/keys || :
	mkdir -p ${_root}/${TMPDIR}/keys/trusted
	mkdir -p ${_root}/${TMPDIR}/keys/revoked
	_fingerprint=$(openssl dgst -sha256 -hex repo.pub | sed 's/^.* //')
	echo "function: sha256" > ${_root}/${TMPDIR}/keys/trusted/key
	echo "fingerprint: \"${_fingerprint}\"" >> ${_root}/${TMPDIR}/keys/trusted/key
	mkdir fakerepo

	cat >> sign.sh << EOF
#!/bin/sh
read sum
[ -z "\$sum" ] && exit 1

$_typecmd
echo SIGNATURE
printf "%s" "\$sum" | $_signcmd
echo
echo CERT
cat repo.pub
echo END
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	atf_check -o ignore -e ignore \
		pkg create -M test.ucl -o fakerepo
	atf_check -o ignore -x \
		pkg repo $_repoargs

	cat >> repo.conf << EOF
local: {
	url: file:///${TMPDIR}/fakerepo
	enabled: true
	signature_type: FINGERPRINTS
	fingerprints: ${TMPDIR}/keys
}
EOF
}

# The signature must have been accepted, i.e. the catalogue was loaded.
check_loaded() {
	atf_check -o inline:"1\n" \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" "$@" rquery -U %v test
}

fingerprint_ecc_body() {
	setup "" "ecc"

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" update
	check_loaded
}

fingerprint_mldsa_body() {
	setup "" "mldsa"

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" update
	check_loaded
}

fingerprint_rsa_body() {
	setup "" "rsa"

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" update
	check_loaded
}

fingerprint_rootdir_body() {
	setup "${TMPDIR}/rootdir" "rsa"

	atf_check \
		-o ignore \
		-e match:".*extracting signature of repo.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" -r "${TMPDIR}/rootdir" update
	check_loaded -r "${TMPDIR}/rootdir"
}
