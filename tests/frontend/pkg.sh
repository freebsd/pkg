#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	pkg_no_database \
	pkg_config_defaults \
	pkg_create_manifest_bad_syntax \
	pkg_repo_load_order \
	double_entry

pkg_no_database_body() {
        atf_skip_on Linux Test fails on Linux

	atf_check \
	    -o empty \
	    -e inline:"${PROGNAME}: package database non-existent\n" \
	    -s exit:1 \
	    env -i PATH="${PATH}" DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH}" LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" pkg -o PKG_DBDIR=/dev/null -N
}

pkg_config_defaults_body()
{
	atf_check \
	    -o match:'^ *PKG_DBDIR = "/var/db/pkg";$' \
	    -o match:'^ *PKG_CACHEDIR = "/var/cache/pkg";$' \
	    -o match:'^ *PORTSDIR = "/usr/d?ports";$' \
	    -o match:'^ *HANDLE_RC_SCRIPTS = false;$' \
	    -o match:'^ *DEFAULT_ALWAYS_YES = false;$' \
	    -o match:'^ *ASSUME_ALWAYS_YES = false;$' \
	    -o match:'^ *PLIST_KEYWORDS_DIR = "";$' \
	    -o match:'^ *SYSLOG = true;$' \
	    -o match:'^ *ALTABI = "[a-zA-Z0-9]+:[a-z\.A-Z0-9]+:[a-zA-Z0-9]+:[a-zA-Z0-9:]+";$' \
	    -o match:'^ *DEVELOPER_MODE = false;$' \
	    -o match:'^ *VULNXML_SITE = "http://vuxml.freebsd.org/freebsd/vuln.xml.xz";$' \
	    -o match:'^ *FETCH_RETRY = 3;$' \
	    -o match:'^ *PKG_PLUGINS_DIR = ".*lib/pkg/";$' \
	    -o match:'^ *PKG_ENABLE_PLUGINS = true;$' \
	    -o match:'^ *DEBUG_SCRIPTS = false;$' \
	    -o match:'^ *PLUGINS_CONF_DIR = ".*/etc/pkg/";$' \
	    -o match:'^ *PERMISSIVE = false;$' \
	    -o match:'^ *REPO_AUTOUPDATE = true;$' \
	    -o match:'^ *NAMESERVER = "";$' \
	    -o match:'^ *EVENT_PIPE = "";$' \
	    -o match:'^ *FETCH_TIMEOUT = 30;$' \
	    -o match:'^ *UNSET_TIMESTAMP = false;$' \
	    -o match:'^ *SSH_RESTRICT_DIR = "";$' \
	    -e empty              \
	    -s exit:0             \
	    env -i ASAN_OPTIONS="${ASAN_OPTIONS}" PATH="${PATH}" DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH}" LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" pkg -C "" -R "" -vv
}

pkg_create_manifest_bad_syntax_body()
{
	mkdir -p testpkg/.metadir
	cat <<EOF >> testpkg/.metadir/+MANIFEST
name: test
version: 1
origin: test
prefix: /usr/local
categories: [test]
comment: this is a test
maintainer: test
www: http://test
desc: <<EOD
A description
EOD
files:
  /usr/local/include/someFile.hp: 'sha256sum' p
EOF
	atf_check \
	    -o empty \
	    -e inline:"${PROGNAME}: Bad format in manifest for key: files\n" \
	    -s exit:1 \
	    pkg create -q -m testpkg/.metadir -r testpkg
}

pkg_repo_load_order_body()
{
	echo "03_repo: { url: file:///03_repo }" > plop.conf
	echo "02_repo: { url: file:///02_repo }" > 02.conf
	echo "01_repo: { url: file:///01_repo }" > 01.conf

	out=$(pkg -o REPOS_DIR=. -vv)
	atf_check \
	    -o match:'.*01_repo\:.*02_repo\:.*03_repo\:.*' \
	    -e empty \
	    -s exit:0 \
	    echo $out
}

double_entry_body()
{
	cat >> pkg.conf <<EOF
pkg_env {}
PKG_ENV : {
 http_proxy: "http://10.0.0.1:3128"
 https_proxy: "http://10.0.0.1:3128"
 ftp_proxy: "http://10.0.0.1:3128"
}
EOF
	atf_check -o ignore pkg -C ./pkg.conf -vv
}
