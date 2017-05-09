#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	empty_conf \
	duplicate_pkgs_notallowed \
	inline_repo \
	nameserver
#	duplicate_pkgs_allowed \

duplicate_pkgs_allowed_body() {
	cat << EOF > pkg.conf
duplicatedefault: 2
EOF

	for n in 1 2; do
		new_pkg test${n} test ${n}
		echo 'allowduplicate: true' >> test${n}.ucl

	atf_check \
		-e empty \
		-o match:"Installing test-${n}..." \
		-s exit:0 \
		pkg register -M test${n}.ucl
done

	atf_check \
		-e empty \
		-o match:"test-1                         a test" \
		-o match:"test-2                         a test" \
		-s exit:0 \
		pkg info
}

duplicate_pkgs_notallowed_body() {
	for n in 1 2; do
		new_pkg test${n} test ${n}
	done

	atf_check \
		-e empty \
		-o match:"Installing test-1..." \
		-s exit:0 \
		pkg register -M test1.ucl

	atf_check \
		-e empty \
		-s exit:70 \
		pkg register -M test1.ucl

	atf_check \
		-e empty \
		-o match:"test-1                         a test" \
		-s exit:0 \
		pkg info
}

empty_conf_body() {
	touch pkg.conf

	cat << EOF > test.ucl
name: test
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /
desc: <<EOD
Yet another test
EOD
EOF

	atf_check \
		-e empty \
		-o ignore \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C pkg.conf info test
}

inline_repo_body() {
	cat > pkgconfiguration << EOF
repositories: {
	pkg1: { url = file:///tmp },
	pkg2: { url = file:///tmp2 },
}
EOF
	atf_check -o match:'^    url             : "file:///tmp",$' \
		-o match:'^    url             : "file:///tmp2",$' \
		pkg -o REPOS_DIR=/dev/null -C pkgconfiguration -vv
}

nameserver_body()
{
	atf_skip_on Darwin Not possible to inject a namserver on OSX
	atf_skip_on Linux Not possible to inject a namserver on OSX

	atf_check \
		-o inline:"\n" \
		pkg -C /dev/null config nameserver

	atf_check \
		-o inline:"192.168.1.1\n" \
		pkg -o NAMESERVER="192.168.1.1" -C /dev/null config nameserver

	atf_check \
		-o inline:"plop\n" \
		-e inline:"${PROGNAME}: Unable to set nameserver, ignoring\n" \
		pkg -o NAMESERVER="plop" -C /dev/null config nameserver
}
