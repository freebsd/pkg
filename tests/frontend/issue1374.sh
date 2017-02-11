#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

# https://github.com/freebsd/pkg/issues/1374
tests_init \
        issue1374

issue1374_body() {

        touch foo.file
        touch pA.file
        touch pB.file

        cat << EOF > foo.ucl
name: foo
origin: lang/foo
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
deps:   {
          pA: {
                origin: "lang/pA",
                version: "1.0"
              },
          pB: { origin: "lang/pB",
                version: "1.0"
          }

        }
files: {
    ${TMPDIR}/foo.file: "",
}
EOF

        cat << EOF > pA.ucl
name: pA
origin: lang/pA
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
files: {
    ${TMPDIR}/pA.file: "",
}
EOF

        cat << EOF > pB.ucl
name: pB
origin: lang/pB
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
files: {
    ${TMPDIR}/pB.file: "",
}
EOF

        cat << EOF > repo1.conf
repo1: {
        url: file://${TMPDIR}/repo1,
        enabled: true
}
EOF

        cat << EOF > repo2.conf
repo2: {
        url: file://${TMPDIR}/repo2,
        enabled: true
}
EOF

        for p in foo pA pB; do
                atf_check \
                        -o ignore \
                        -e empty \
                        -s exit:0 \
                        pkg create -o ${TMPDIR}/repo1 -M ./${p}.ucl
        done

        atf_check \
                -o inline:"Creating repository in ${TMPDIR}/repo1:  done\nPacking files for repository:  done\n" \
                -e empty \
                -s exit:0 \
                pkg repo -o ${TMPDIR}/repo1 ${TMPDIR}/repo1


        cat << EOF > pB.ucl
name: pB
origin: lang/pB
version: "1.1"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
files: {
    ${TMPDIR}/pB.file: "",
}
EOF


        for p in foo pA pB; do
                atf_check \
                        -o ignore \
                        -e empty \
                        -s exit:0 \
                        pkg create -o ${TMPDIR}/repo2 -M ./${p}.ucl
        done

        atf_check \
                -o inline:"Creating repository in ${TMPDIR}/repo2:  done\nPacking files for repository:  done\n" \
                -e empty \
                -s exit:0 \
                pkg repo -o ${TMPDIR}/repo2 ${TMPDIR}/repo2


        atf_check \
                -o ignore \
		-e match:".*load error: access repo file.*" \
                -s exit:0 \
                pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" install -y foo

        atf_check \
                -o ignore \
                -e empty \
                -s exit:0 \
                pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" delete -y foo



        atf_check \
                -o ignore \
                -e empty \
                -s exit:0 \
                pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" autoremove -y

	# 100% must be empty, but it's not
        atf_check \
                -o empty \
                -e empty \
                -s exit:0 \
                pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" query -e "%a == 0" "%n-%v"


}

