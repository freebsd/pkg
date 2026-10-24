#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	pubkey_ecdsa \
	pubkey_eddsa \
	pubkey_mldsa \
	pubkey_mldsa_badkey \
	pubkey_mldsa_wrongparam \
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
			openssl ec -inform DER -in repo.key -pubout -out repo.pub -outform DER
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

	atf_check -o inline:"1\n" \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" rquery -U %v test
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

	atf_check -o inline:"1\n" \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" rquery -U %v test
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

	atf_check -o inline:"1\n" \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" rquery -U %v test
}


mldsa_setup() {
	atf_require_signer mldsa

	atf_check -o save:repo.pub -e ignore \
		pkg key --create -t mldsa repo.key
	mkdir fakerepo

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 /

	atf_check -o ignore -e ignore \
		pkg create -M test.ucl -o fakerepo
}

mldsa_repo_conf() {
	cat >> repo.conf << EOF
local: {
	url: file:///${TMPDIR}/fakerepo
	enabled: true
	pubkey: ${TMPDIR}/$1
	signature_type: "pubkey"
}
EOF
}

pubkey_mldsa_body() {
	mldsa_setup
	atf_check -o ignore -e ignore \
		pkg repo fakerepo mldsa:repo.key
	mldsa_repo_conf repo.pub

	atf_check \
		-o ignore \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" update

	atf_check -o inline:"1\n" \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" rquery -U %v test
}

# A repository signed with one ML-DSA key must not verify against another.
pubkey_mldsa_badkey_body() {
	mldsa_setup
	atf_check -o save:other.pub -e ignore \
		pkg key --create -t mldsa other.key
	atf_check -o ignore -e ignore \
		pkg repo fakerepo mldsa:repo.key
	mldsa_repo_conf other.pub

	# pkg update does not fail on a bad signature, it drops the catalogue.
	atf_check \
		-o ignore \
		-e match:"Invalid signature" \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" update

	atf_check -s not-exit:0 -o empty -e ignore \
		pkg -o REPOS_DIR="${TMPDIR}" \
		-o ${PKG_CACHEDIR}="${TMPDIR}" rquery -U %v test
}

# The mldsa signer only accepts ML-DSA-87 keys.
pubkey_mldsa_wrongparam_head() {
	atf_set "require.progs" "openssl"
}
pubkey_mldsa_wrongparam_body() {
	mldsa_setup
	if ! openssl list -signature-algorithms 2>/dev/null | \
	    grep -q ML-DSA-65; then
		atf_skip "openssl does not support ML-DSA-65"
	fi
	atf_check openssl genpkey -algorithm ML-DSA-65 -out repo65.key

	atf_check -s not-exit:0 -o ignore -e match:"signing failed" \
		pkg repo fakerepo mldsa:repo65.key
}
