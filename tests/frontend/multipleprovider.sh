#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	multiple_providers

multiple_providers_body() {
	touch file

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 test1 1
	cat << EOF >> pkg1.ucl
shlibs_provided [
	"lib1.so.6"
]
files: {
	${TMPDIR}/file: ""
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg2 dep 1
	cat << EOF >> pkg2.ucl
shlibs_required [
	"lib1.so.6"
]
deps: {
	test1 {
		origin: test
		version: 1
	}
}
EOF

	for p in pkg1 pkg2; do
		atf_check \
			-o match:".*Installing.*\.\.\.$" \
			-e empty \
			-s exit:0 \
			pkg register -M ${p}.ucl
	done

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg3 test1 1_0
	cat << EOF >> pkg3.ucl
shlibs_provided [
	"lib1.so.6"
]
files: {
	${TMPDIR}/file: ""
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg4 test2 1
	cat << EOF >> pkg4.ucl
shlibs_provided [
	"lib1.so.6"
]
files: {
	${TMPDIR}/file: ""
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg5 dep 1_1
	cat << EOF >> pkg5.ucl
shlibs_required [
	"lib1.so.6"
]
deps: {
	test2 {
		origin: test
		version: 1
	}
}
EOF

	for p in pkg3 pkg4 pkg5; do
		atf_check \
			-o ignore \
			-e empty \
			-s exit:0 \
			 pkg create -M ./${p}.ucl
	done

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	cat << EOF > repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="$TMPDIR" -o PKG_CACHEDIR="$TMPDIR" upgrade -y
}

