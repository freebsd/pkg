#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	pubkey_ecdsa \
	pubkey_eddsa \
	pubkey_rsa \
	pubkey_legacy

ecc_test() {
	local _type

	_type="$1"

	case "$_type" in
	eddsa)
		atf_check -o save:repo.pub -e ignore \
			pkg key --create -t "$_type" repo.key
		;;
	ecdsa)
		# pkg can generate these, but we want to be sure that we're still
		# compatible with what openssl produces.
		atf_check -o ignore -e ignore \
			openssl ecparam -genkey -name secp256k1 -out repo.key -outform DER
		chmod 0400 repo.key
		atf_check -o ignore -e ignore \
			openssl ec -in repo.key -pubout -out repo.pub -outform DER
		;;
	esac

	mkdir fakerepo

	sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 /

	atf_check -o ignore -e ignore \
		pkg create -M test.ucl -o fakerepo
	atf_check -o ignore -e ignore \
		pkg repo fakerepo "$_type":repo.key
	cat >> repo.conf << EOF
local: {
	url: file:///${TMPDIR}/fakerepo
	enabled: true
	pubkey: ${TMPDIR}/repo.pub
	signature_type: "pubkey"
}
EOF
	atf_check \
		-o ignore \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" update
}

pubkey_ecdsa_body() {
	ecc_test "ecdsa"
}

pubkey_eddsa_body() {
	ecc_test "eddsa"
}

pubkey_rsa_body() {
	atf_check -o save:repo.pub -e ignore \
		pkg key --create repo.key
	mkdir fakerepo

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 /

	atf_check -o ignore -e ignore \
		pkg create -M test.ucl -o fakerepo
	atf_check -o ignore -e ignore \
		pkg repo fakerepo rsa:repo.key
	cat >> repo.conf << EOF
local: {
	url: file:///${TMPDIR}/fakerepo
	enabled: true
	pubkey: ${TMPDIR}/repo.pub
	signature_type: "pubkey"
}
EOF
	atf_check \
		-o ignore \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" update
}

# Legacy format, unprefixed key passed to pkg-repo
pubkey_legacy_body() {
	atf_check -o save:repo.pub -e ignore \
		pkg key --create repo.key
	mkdir fakerepo

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 /

	atf_check -o ignore -e ignore \
		pkg create -M test.ucl -o fakerepo
	atf_check -o ignore -e ignore \
		pkg repo fakerepo repo.key
	cat >> repo.conf << EOF
local: {
	url: file:///${TMPDIR}/fakerepo
	enabled: true
	pubkey: ${TMPDIR}/repo.pub
	signature_type: "pubkey"
}
EOF
	atf_check \
		-o ignore \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" update
}

