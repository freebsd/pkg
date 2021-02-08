#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	key_create \
	key_pubout

key_create_head() {
	atf_set "require.progs" "openssl"
}
key_create_body() {
	echo "secure msg" > msg

	atf_check -o ignore -e ignore -x pkg key --create -t rsa repo
	# Group permissions are OK, but let's strive for limited to the user.
	atf_check -o match:'-{6}$' -x \
	    'ls -l repo | cut -c1-10'
	# Should have also created the corresponding pub key.
	atf_check test -f repo.pub

	# Make sure it's functional.
	atf_check -o save:msg.sign openssl dgst -sign repo -sha256 -binary msg
	atf_check -o ignore openssl dgst -sha256 -verify repo.pub -signature msg.sign msg
}

key_pubout_head() {
	atf_set "require.progs" "openssl"
}
key_pubout_body() {
	echo "secure msg" > msg

	atf_check -o ignore -e ignore -x pkg key --create -t rsa repo
	# Oops, we lost the public key.
	rm repo.pub
	atf_check test ! -f repo.pub
	atf_check -o save:repo.pub pkg key --public -t rsa repo

	# Make sure it's functional.
	atf_check -o save:msg.sign openssl dgst -sign repo -sha256 -binary msg
	atf_check -o ignore openssl dgst -sha256 -verify repo.pub -signature msg.sign msg
}


