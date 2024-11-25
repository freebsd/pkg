#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	key_create \
	key_pubout \
	key_sign

key_create_head() {
	atf_set "require.progs" "openssl"
}
key_create_body() {
	echo "secure msg" > msg

	atf_check -o save:repo.pub -e ignore -x pkg key --create -t rsa \
	    repo.key

	# Group permissions are OK, but let's strive for limited to the user.
	# This doesn't use stat(1) to side-step the differences between
	# platforms in how to request specific fields; the ls(1) mode
	# representation is usually consistent enough.
	atf_check -o match:'-{6}$' -x 'ls -l repo.key | cut -c1-10'
	# Should have also output the corresponding pub key.
	atf_check test -s repo.pub

	# Make sure it's functional.
	atf_check -o save:msg.sign openssl dgst -sign repo.key -sha256 \
	    -binary msg
	atf_check -o ignore openssl dgst -sha256 -verify repo.pub \
	    -signature msg.sign msg

	for signer in ecc ecdsa eddsa; do
		rm -f repo.key repo.pub
		atf_check -o save:repo.pub -e ignore -x pkg key --create \
		    -t "$signer" repo.key

		atf_check -o match:'-{6}$' -x 'ls -l repo.key | cut -c1-10'
		atf_check test -s repo.pub
	done
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

key_sign_head() {
	atf_set "require.progs" "openssl"
}
key_sign_body() {
	echo "secure msg" > msg

	for signer in rsa ecdsa; do
		rm -f repo.key repo.pub msg.sig

		# Generate a key with pkg
		atf_check -o save:repo.pub -e ignore \
		    pkg key --create -t "$signer" repo.key
		
		atf_check -o save:msg.sig \
		    pkg key --sign -t "$signer" repo.key < msg

		if [ $signer = ecdsa ]; then
			keyform="-keyform DER"
		else
			keyform=""
		fi

		atf_check -o ignore openssl dgst -sha256 $keyform -verify repo.pub \
		    -signature msg.sig msg
	done
}
