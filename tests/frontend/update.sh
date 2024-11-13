#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	update_error \
	file_url \

update_error_body() {

	mkdir repos
	mkdir empty
	cat > repos/test.conf << EOF
test: {
  url: "file://empty/",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid url: 'file://empty//meta.conf'" \
		-s exit:1 \
		pkg -R repos update
}

file_url_body() {
	mkdir repos
	touch meta.conf
	here=$(pwd)


#
# test file:/empty/, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:/empty/",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"invalid url: 'file:/empty//meta.conf" \
		-s exit:1 \
		pkg -R repos update

#
# test file://here, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://here",
}
EOF
	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid url: 'file://here/meta.conf'" \
		-s exit:1 \
		pkg -R repos update


#
# test file://here//path, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://here/${here}",
}
EOF
	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file:////path, which is valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:///${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file:///path, which is valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file://path, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:/${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid url: 'file:/${here}/meta.conf'" \
		-s exit:1 \
		pkg -R repos update


#
# test file://localhost/path, which is a valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://localhost${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

}
