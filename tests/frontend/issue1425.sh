#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

# https://github.com/freebsd/pkg/issues/1425
#pkgA
# - pkgB
#    - pkgC
#      - pkgD
#1. Two repos (repoA and repoB) with same set of packages. repoB last in the list, so all packages must be prefered from this one.
#2. On upgrade we must stick to same repo
#3. Two repos (repoA and repoB) with same set of packages. repoA has a higher priority, so all packages must be prefered from this one.

tests_init \
        issue1425

issue1425_body() {

        touch pkgA.file
        touch pkgB.file
        touch pkgC.file
        touch pkgD.file

        cat << EOF > pkgA.ucl
name: pkgA
origin: misc/pkgA
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
          pkgB: {
                origin: "misc/pkgB",
                version: "1.0"
              }
        }
files: {
    ${TMPDIR}/pkgA.file: "",
}
EOF

        cat << EOF > pkgB.ucl
name: pkgB
origin: misc/pkgB
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
          pkgC: {
                origin: "misc/pkgC",
                version: "1.0"
              }
        }

files: {
    ${TMPDIR}/pkgB.file: "",
}
EOF

        cat << EOF > pkgC.ucl
name: pkgC
origin: misc/pkgC
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
          pkgD: {
                origin: "misc/pkgD",
                version: "1.0"
              }
        }
files: {
    ${TMPDIR}/pkgC.file: "",
}
EOF


        cat << EOF > pkgD.ucl
name: pkgD
origin: misc/pkgD
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
    ${TMPDIR}/pkgD.file: "",
}
EOF

	mkdir reposconf
        cat << EOF > reposconf/repos.conf
repoA: {
        url: file://${TMPDIR}/repoA,
        enabled: true
}
repoB: {
        url: file://${TMPDIR}/repoB,
        enabled: true
}

EOF

        for p in pkgA pkgB pkgC pkgD; do
                atf_check \
                        -o ignore \
                        -e empty \
                        -s exit:0 \
                        pkg create -o ${TMPDIR}/repoA -M ./${p}.ucl
        done

        atf_check \
                -o inline:"Creating repository in ${TMPDIR}/repoA:  done\nPacking files for repository:  done\n" \
                -e empty \
                -s exit:0 \
                pkg repo -o ${TMPDIR}/repoA ${TMPDIR}/repoA


        for p in pkgA pkgB pkgC pkgD; do
                atf_check \
                        -o ignore \
                        -e empty \
                        -s exit:0 \
                        pkg create -o ${TMPDIR}/repoB -M ./${p}.ucl
        done

        atf_check \
                -o inline:"Creating repository in ${TMPDIR}/repoB:  done\nPacking files for repository:  done\n" \
                -e empty \
                -s exit:0 \
                pkg repo -o ${TMPDIR}/repoB ${TMPDIR}/repoB

OUTPUT_CASE1="Updating repoA repository catalogue...
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data.pkg:  done
Processing entries:  done
repoA repository update completed. 4 packages processed.
Updating repoB repository catalogue...
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data.pkg:  done
Processing entries:  done
repoB repository update completed. 4 packages processed.
All repositories are up to date.
Checking integrity... done (0 conflicting)
The following 4 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	pkgA: 1.0 [repoB]
	pkgB: 1.0 [repoA]
	pkgC: 1.0 [repoA]
	pkgD: 1.0 [repoA]

Number of packages to be installed: 4
${JAILED}[1/4] Installing pkgD-1.0...
${JAILED}[1/4] Extracting pkgD-1.0:  done
${JAILED}[2/4] Installing pkgC-1.0...
${JAILED}[2/4] Extracting pkgC-1.0:  done
${JAILED}[3/4] Installing pkgB-1.0...
${JAILED}[3/4] Extracting pkgB-1.0:  done
${JAILED}[4/4] Installing pkgA-1.0...
${JAILED}[4/4] Extracting pkgA-1.0:  done
"

        atf_check \
                -o inline:"${OUTPUT_CASE1}" \
                -s exit:0 \
                pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" install -y pkgA

	#rm -f ${TMPDIR}/local.sqlite

        cat << EOF > reposconf/repos.conf
repoA: {
        url: file://${TMPDIR}/repoA,
        enabled: true,
	priority: 100
}
repoB: {
        url: file://${TMPDIR}/repoB,
        enabled: true
}
EOF


OUTPUT_CASE2="Updating repoA repository catalogue...
repoA repository is up to date.
Updating repoB repository catalogue...
repoB repository is up to date.
All repositories are up to date.
Checking integrity... done (0 conflicting)
The following 2 package(s) will be affected (of 0 checked):

Installed packages to be REINSTALLED:
	pkgA-1.0 [repoB]
	pkgD-1.0 [repoA]

Number of packages to be reinstalled: 2
${JAILED}[1/2] Reinstalling pkgA-1.0...
${JAILED}[1/2] Extracting pkgA-1.0:  done
${JAILED}[2/2] Reinstalling pkgD-1.0...
${JAILED}[2/2] Extracting pkgD-1.0:  done
"

        atf_check \
                -o inline:"${OUTPUT_CASE2}" \
                -e empty \
                -s exit:0 \
                pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" upgrade -yf pkgA pkgD


	rm -f ${TMPDIR}/local.sqlite


OUTPUT_CASE3="Updating repoA repository catalogue...
repoA repository is up to date.
Updating repoB repository catalogue...
repoB repository is up to date.
All repositories are up to date.
Checking integrity... done (0 conflicting)
The following 4 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	pkgA: 1.0 [repoA]
	pkgB: 1.0 [repoA]
	pkgC: 1.0 [repoA]
	pkgD: 1.0 [repoA]

Number of packages to be installed: 4
${JAILED}[1/4] Installing pkgD-1.0...
${JAILED}[1/4] Extracting pkgD-1.0:  done
${JAILED}[2/4] Installing pkgC-1.0...
${JAILED}[2/4] Extracting pkgC-1.0:  done
${JAILED}[3/4] Installing pkgB-1.0...
${JAILED}[3/4] Extracting pkgB-1.0:  done
${JAILED}[4/4] Installing pkgA-1.0...
${JAILED}[4/4] Extracting pkgA-1.0:  done
"

        atf_check \
                -o inline:"${OUTPUT_CASE3}" \
                -e empty \
                -s exit:0 \
                pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" install -y pkgA
}
