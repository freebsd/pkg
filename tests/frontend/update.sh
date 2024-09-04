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
		-e match:"pkg: file://empty//packagesite.pkg: No such file or directory" \
		-s exit:1 \
		pkg -R repos update
}

file_url_body() {
	mkdir repos
	touch meta.conf
	here=$(pwd)

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

	cat > repos/test.conf << EOF
test: {
  url: "file://here",
}
EOF
	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update


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

	cat > repos/test.conf << EOF
test: {
  url: "file:/${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update
}
