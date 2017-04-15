#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	fingerprint

fingerprint_body() {
	atf_check -o ignore -e ignore \
		openssl genrsa -out repo.key 2048
	rm -rf ${TMPDIR}/keys || :
	mkdir -p keys/trusted
	mkdir -p keys/revoked
	chmod 0400 repo.key
	atf_check -o ignore -e ignore \
		openssl rsa -in repo.key -out repo.pub -pubout
	echo "function: sha256" > keys/trusted/key
	echo -n "fingerprint: " >> keys/trusted/key
	openssl dgst -sha256 -hex repo.pub | sed 's/^.* //' >> keys/trusted/key
	echo "" >> keys/trusted/key
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

	cat >> test.ucl << EOF
name: test
origin: test
version: "1"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /
abi = "*";
desc: <<EOD
Yet another test
EOD
EOF

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
	atf_check \
		-o ignore \
		-e match:".*load error: access repo file.*" \
		pkg -dd -o REPOS_DIR="${TMPDIR}" \
		-o PKG_CACHEDIR="${TMPDIR}" update
}
