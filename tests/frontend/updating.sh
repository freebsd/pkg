#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	updating_all_users \
        updating_pkg \
	updating_perl \
	updating_samba \
        updating_ilmbase \
        updating_mysql \
        updating_postgresql \
        updating_cupsbase \
        updating_cups \

updating_all_users_body() {
	cat > UPDATING <<EOF
20190624:
  AFFECTS: all users
  AUTHOR: ports@FreeBSD.org

  Messages...
20190625:
  AFFECTS: all ports users
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190624:$" \
		-o match:"^20190625:$" \
		pkg updating -f UPDATING
}

updating_pkg_body() {
	cat > test.ucl << EOF
name: "pkg"
origin: "ports-mgmt/pkg"
version: "1.10.5"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190619:
  AFFECTS: ports-mgmt/pkg
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190619:$" \
		pkg updating -f UPDATING
}

updating_perl_body() {
	cat > test.ucl << EOF
name: "perl5.26"
origin: "lang/perl5.26"
version: "5.26_3"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190620:
  AFFECTS: lang/perl5.*
  AUTHOR: ports@FreeBSD.org

  Messages...

20190621:
  AFFECTS: lang/perl5*
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190620:$" \
		-o match:"^20190621:$" \
		pkg updating -f UPDATING
}

updating_samba_body() {
	cat > test.ucl << EOF
name: "samba47"
origin: "net/samba47"
version: "4.7.12"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190622:
  AFFECTS: net/samba4[678]
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190622:$" \
		pkg updating -f UPDATING
}

updating_ilmbase_body() {
	cat > test.ucl << EOF
name: "ilmbase"
origin: "graphics/ilmbase"
version: "2.3.0_2"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190623:
  AFFECTS: users of graphics/ilmbase, graphics/OpenEXR
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190623:$" \
		pkg updating -f UPDATING
}

updating_mysql_body() {
	cat > test.ucl << EOF
name: "mysql55-server"
origin: "databases/mysql55-server"
version: "5.5.62_1"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190626:
  AFFECTS: users of databases/mysql55-(server|client)
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190626:$" \
		pkg updating -f UPDATING
}

updating_postgresql_body() {
	cat > test.ucl << EOF
name: "postgresql95-server"
origin: "databases/postgresql95-server"
version: "9.5.17"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190627:
  AFFECTS: users of databases/postgresql??-(server|client)
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190627:$" \
		pkg updating -f UPDATING
}

updating_cupsbase_body() {
	cat > test.ucl << EOF
name: "cups-base"
origin: "print/cups-base"
version: "2.2.1"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190628:
  AFFECTS: users of print/cups-{base,client,image}
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190628:$" \
		pkg updating -f UPDATING
}

updating_cups_body() {
	cat > test.ucl << EOF
name: "cups"
origin: "print/cups"
version: "2.2.1"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190628:
  AFFECTS: users of print/cups-{base,client,image}
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o empty \
		-e empty \
		pkg updating -f UPDATING
}
