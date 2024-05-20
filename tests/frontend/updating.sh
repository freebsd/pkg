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
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test pkg 1.10.5 /usr/local
	cat >> test.ucl << EOF
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190619:
  AFFECTS: pkg
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190619:$" \
		pkg updating -f UPDATING
}

updating_perl_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test perl5.26 5.26 /usr/local
	cat >> test.ucl << EOF
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190620:
  AFFECTS: perl5.*
  AUTHOR: ports@FreeBSD.org

  Messages...

20190621:
  AFFECTS: perl5*
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190620:$" \
		-o match:"^20190621:$" \
		pkg updating -f UPDATING
}

updating_samba_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test samba47 4.7.12 /usr/local
	cat >> test.ucl << EOF
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190622:
  AFFECTS: samba4[678]
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190622:$" \
		pkg updating -f UPDATING
}

updating_ilmbase_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test ilmbase 2.3.0_2 /usr/local
	cat >> test.ucl << EOF
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190623:
  AFFECTS: users of ilmbase, graphics/OpenEXR
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190623:$" \
		pkg updating -f UPDATING
}

updating_mysql_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test mysql55-server 5.5.62_1 /usr/local
	cat >> test.ucl << EOF
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190626:
  AFFECTS: users of mysql55-(server|client)
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190626:$" \
		pkg updating -f UPDATING
}

updating_postgresql_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test postgresql95-server 9.5.17 /usr/local
	cat >> test.ucl << EOF
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190627:
  AFFECTS: users of postgresql??-(server|client)
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190627:$" \
		pkg updating -f UPDATING
}

updating_cupsbase_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test cups-base 2.2.1 /usr/local
	cat >> test.ucl << EOF
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190628:
  AFFECTS: users of cups-{base,client,image}
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o match:"^20190628:$" \
		pkg updating -f UPDATING
}

updating_cups_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test cups 2.2.1 /usr/local
	cat >> test.ucl << EOF
message: [
	{ message: "Always print" }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl

	cat > UPDATING <<EOF
20190628:
  AFFECTS: users of cups-{base,client,image}
  AUTHOR: ports@FreeBSD.org

  Messages...
EOF

	atf_check \
		-o empty \
		-e empty \
		pkg updating -f UPDATING
}
