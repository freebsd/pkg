#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	pkg_phpextensions

pkg_phpextensions_body() {
	touch php53.file
	touch php53extension.file
	touch php53gd.file
	touch php53fileinfo.file

	cat << EOF > php53.ucl
name: php53
origin: lang/php53
version: "5.3.27"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
provides: [ php-5.3.27 ]
files: {
    ${TMPDIR}/php53.file: "",
}
EOF

	cat << EOF > php53extension.ucl
name: php53-extensions
origin: lang/php53-extensions
version: "1.6"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
requires: [ php-5.3.27 ]
deps:   {
          php53-gd: {
                origin: "graphics/php53-gd",
                version: "5.3.27"
          }

        }

files: {
    ${TMPDIR}/php53extension.file: "",
}
EOF

	cat << EOF > php53gd.ucl
name: php53-gd
origin: graphics/php53-gd
version: "5.3.27"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
requires: [ php-5.3.27 ]
deps:   {
          php53: {
                origin: "lang/php53",
                version: "5.3.27"
          }

        }

files: {
    ${TMPDIR}/php53gd.file: "",
}
EOF

	cat << EOF > repo1.conf
local1: {
        url: file://${TMPDIR},
        enabled: true
}
EOF

	for p in php53 php53extension php53gd; do
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

	atf_check \
		-o ignore \
		-e match:".*load error: access repo file.*" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" install -y php53-extensions

#### NEW

	rm repo1.conf
	rm -f *.ucl
	rm *.txz

	cat << EOF > php53.new.ucl
name: php53
origin: lang/php53
version: "5.3.40"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
provides: [ php-5.3.40 ]
files: {
    ${TMPDIR}/php53.file: "",
}
EOF

	cat << EOF >> php53extension.new.ucl
name: php53-extensions
origin: lang/php53-extensions
version: "1.6"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
requires: [ php-5.3.40 ]
deps:   {
          php53-gd: {
                origin: "graphics/php53-gd",
                version: "5.3.40"
          }

        }

files: {
    ${TMPDIR}/php53extension.file: "",
}
EOF

	cat << EOF >> php53gd.new.ucl
name: php53-gd
origin: graphics/php53-gd
version: "5.3.40"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
requires: [ php-5.3.40 ]
deps:   {
          php53: {
                origin: "lang/php53",
                version: "5.3.40"
          }

        }

files: {
    ${TMPDIR}/php53gd.file: "",
}
EOF


	cat << EOF >> php53fileinfo.new.ucl
name: php53-fileinfo
origin: sysutils/php53-fileinfo
version: "5.3.40"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
requires: [ php-5.3.40 ]
deps:   {
          php53: {
                origin: "lang/php53",
                version: "5.3.40"
          }

        }

files: {
    ${TMPDIR}/php53fileinfo.file: "",
}
EOF

	for p in php53 php53extension php53gd php53fileinfo; do
		atf_check \
			-o ignore \
			-e empty \
			-s exit:0 \
			pkg create -M ./${p}.new.ucl
	done

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	cat << EOF >> repo.conf
local: {
        url: file://${TMPDIR}/,
        enabled: true
}
EOF

OUTPUT="php53-5.3.40
php53-extensions-1.6
php53-fileinfo-5.3.40
php53-gd-5.3.27
"

OUTPUT="Updating local repository catalogue...
${JAILED}Fetching meta.txz:  done
${JAILED}Fetching packagesite.txz:  done
Processing entries:  done
local repository update completed. 4 packages processed.
All repositories are up to date.
Checking integrity... done (0 conflicting)
The following 4 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	php53-fileinfo: 5.3.40

Installed packages to be UPGRADED:
	php53: 5.3.27 -> 5.3.40
	php53-gd: 5.3.27 -> 5.3.40

Installed packages to be REINSTALLED:
	php53-extensions-1.6 (requires changed)

Number of packages to be installed: 1
Number of packages to be upgraded: 2
Number of packages to be reinstalled: 1
"

	atf_check \
		-o inline:"${OUTPUT}" \
		-e match:".*load error: access repo file.*" \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" install -n php53-fileinfo


OUTPUT="Updating local repository catalogue...
local repository is up to date.
All repositories are up to date.
Checking for upgrades (3 candidates):  done
Processing candidates (3 candidates):  done
Checking integrity... done (0 conflicting)
The following 3 package(s) will be affected (of 0 checked):

Installed packages to be UPGRADED:
	php53-gd: 5.3.27 -> 5.3.40
	php53: 5.3.27 -> 5.3.40

Installed packages to be REINSTALLED:
	php53-extensions-1.6 (requires changed)

Number of packages to be upgraded: 2
Number of packages to be reinstalled: 1
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" upgrade -n
}
