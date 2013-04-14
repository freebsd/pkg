#! /usr/bin/env atf-sh

atf_test_case annotate
annotate_head() {
	atf_set "descr" "pkg annotate"
	atf_set "require.files" \
	   "$(atf_get_srcdir)/png-1.5.14.yaml $(atf_get_srcdir)/sqlite3-3.7.14.1.yaml"
}

annotate_body() {
        export PKG_DBDIR=$HOME/pkg
        export INSTALL_AS_USER=yes

	mkdir -p $PKG_DBDIR || atf_fail "can't create $PKG_DBDIR"

	for pkg in 'png-1.5.14' 'sqlite3-3.7.14.1' ; do
	    atf_check \
		-o match:"^Installing $pkg\.\.\." \
		-e empty \
		-s exit:0 \
		pkg register -t -M $(atf_get_srcdir)/$pkg.yaml
	done

	[ -f "$PKG_DBDIR/local.sqlite" ] || \
	    atf_fail "Can't populate $PKG_DBDIR/local.sqlite"

        atf_check \
	    -o match:"added annotation tagged: TEST1" \
	    -s exit:0 \
	    pkg annotate -Ay png TEST1 test1

	atf_check \
	    -o match:"TEST1 +: test1" \
	    -s exit:0 \
	    pkg info -A png

	echo test2 > $HOME/annotate-TEST2.txt

	atf_check \
	    -o match:"added annotation tagged: TEST2" \
	    -s exit:0 \
	    pkg annotate -Ay png TEST2 < $HOME/annotate-TEST2.txt

	atf_check \
	    -o match:"TEST1 +: test1" \
	    -o match:"TEST2 +: test2" \
	    -s exit:0 \
	    pkg info -A png

	atf_check \
	    -o match:"Modified annotation tagged: TEST1" \
	    -s exit:0 \
	    pkg annotate -My png TEST1 test1-modified

	atf_check \
	    -o match:"TEST1 +: test1-modified" \
	    -o match:"TEST2 +: test2" \
	    -s exit:0 \
	    pkg info -A png

	atf_check \
	    -o match:"Deleted annotation tagged: TEST1" \
	    -s exit:0 \
	    pkg annotate -Dy png TEST1

	atf_check \
	    -o not-match:"TEST1" \
	    -o match:"TEST2 +: test2" \
	    -s exit:0 \
	    pkg info -A png

	atf_check \
	    -o match:"Deleted annotation tagged: TEST2" \
	    -s exit:0 \
	    pkg annotate -Dy png TEST2

	atf_check \
	    -o not-match:"TEST1" \
	    -o not-match:"TEST2" \
	    -s exit:0 \
	    pkg info -A png
}

atf_init_test_cases() {
        eval `cat $(atf_get_srcdir)/test_environment`

	# Tests are run in alphabetical order
	atf_add_test_case annotate

}
