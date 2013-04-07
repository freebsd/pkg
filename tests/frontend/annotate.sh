#! /usr/bin/env atf-sh

atf_test_case ann0
ann0_head() {
	atf_set "descr" "set up dummy PKG database"
}

ann0_body() {
    mkdir -p $PKG_DBDIR || atf_fail "can't create $PKG_DBDIR"

    

}

atf_test_case ann1
ann1_head() {
	atf_set "descr" "add annotation via command-line"
}

ann1_body() {
        atf_check \
	    -o match:"added annotation tagged: TEST1" \
	    -s exit:0 \
	    pkg annotate -Ay pkg TEST1 test1

	atf_check \
	    -o match:"TEST1 +: test1" \
	    -s exit:0 \
	    pkg info -A pkg
}

atf_test_case ann2
ann2_head()
{
	atf_set "descr" "add annotation from stdin"
}
ann2_body()
{
	echo test2 > $HOME/annotate-TEST2.txt

	atf_check \
	    -o match:"added annotation tagged: TEST2" \
	    -s exit:0 \
	    pkg annotate -Ay pkg TEST2 < $HOME/annotate-TEST2.txt

	atf_check \
	    -o match:"TEST1 +: test1" \
	    -o match:"TEST2 +: test2" \
	    -s exit:0 \
	    pkg info -A pkg

}

atf_test_case ann3
ann3_head()
{
	atf_set "descr" "modify annotation via command-line"
}
ann3_body()
{
	atf_check \
	    -o match:"Modified annotation tagged: TEST1" \
	    -s exit:0 \
	    pkg annotate -My pkg TEST1 test1-modified

	atf_check \
	    -o match:"TEST1 +: test1-modified" \
	    -o match:"TEST2 +: test2" \
	    -s exit:0 \
	    pkg info -A pkg
}

atf_test_case ann4
ann4_head()
{
	atf_set "descr" "delete one annotation from several"
}
ann4_body()
{
	atf_check \
	    -o match:"Deleted annotation tagged: TEST1" \
	    -s exit:0 \
	    pkg annotate -Dy pkg TEST1

	atf_check \
	    -o not-match:"TEST1" \
	    -o match:"TEST2 +: test2" \
	    -s exit:0 \
	    pkg info -A pkg
}

atf_test_case ann5
ann5_head()
{
	atf_set "descr" "delete the second annotation"
}
ann5_body()
{
	atf_check \
	    -o match:"Deleted annotation tagged: TEST2" \
	    -s exit:0 \
	    pkg annotate -Dy pkg TEST2

	atf_check \
	    -o not-match:"TEST1" \
	    -o not-match:"TEST2" \
	    -s exit:0 \
	    pkg info -A pkg
}

atf_init_test_cases() {
        eval `cat $(atf_get_srcdir)/test_environment`

	# Tests are run in alphabetical order
	for tc in ann1 ann2 ann3 ann4 ann5; do
	    atf_add_test_case $tc
	done
}
