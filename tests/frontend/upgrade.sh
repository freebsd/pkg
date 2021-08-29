#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	issue1881 \
	issue1881_newdep \
	three_digit_revision

issue1881_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 pkg_a 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg2 pkg_a 1_1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg3 pkg_b 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg4 pkg_b 1_1

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M pkg1.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg3.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg2.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg4.ucl

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir repoconf
	cat << EOF > repoconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o not-match:"^[[:space:]]+pkg_b: 1$" \
		-e ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="$TMPDIR/repoconf" -o PKG_CACHEDIR="$TMPDIR" upgrade -yx '^pkg_'
}

issue1881_newdep_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 pkg_a 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg2 pkg_a 1_1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg3 pkg_b 1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg4 pkg_b 1_1

	cat <<EOF >> ./pkg2.ucl
deps: {
	pkg_b: {
		origin: "wedontcare",
		version: "1"
	}
}
EOF

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M pkg1.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg3.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg2.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg4.ucl

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir repoconf
	cat << EOF > repoconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o match:"^[[:space:]]+pkg_b: 1_1$" \
		-e ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="$TMPDIR/repoconf" -o PKG_CACHEDIR="$TMPDIR" upgrade -yx '^pkg_'
}

three_digit_revision_body() {

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 pkg_a 1_90

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg register -M pkg1.ucl

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 pkg_a 1_125

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg1.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir repoconf
	cat << EOF > repoconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="$TMPDIR/repoconf" -o PKG_CACHEDIR="$TMPDIR" upgrade -yx '^pkg_'
	atf_check \
		-o inline:"pkg_a-1_125\n" \
		-e empty \
		-s exit:0 \
		pkg info -q
}
