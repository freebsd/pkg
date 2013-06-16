#! /usr/bin/env atf-sh

atf_test_case pkg_no_database
pkg_no_database_head() {
	atf_set "descr" "testing pkg -- no database"
}

pkg_no_database_body() {
    atf_check \
	-o empty \
	-e inline:"pkg: package database non-existent\n" \
	-s exit:69 \
	-x PKG_DBDIR=/dev/null pkg -N
}

atf_test_case pkg_version
pkg_version_head()
{
	atf_set "descr" "testing pkg -- latest compiled version"
}

pkg_version_body()
{
        NEWVERS_SH="$( atf_get_srcdir )/../../newvers.sh"
	eval $($NEWVERS_SH)
	
	[ ${PKGVERSION} ] || atf_fail 'eval $(newvers.sh) failed'

	atf_check -o match:"^${PKGVERSION} " -e empty -s exit:0 pkg -v
}


atf_test_case pkg_config_defaults
pkg_config_defaults_head()
{
	atf_set "descr" "testing pkg -- compiled-in defaults"
}

pkg_config_defaults_body()
{
    atf_check                 \
	-o match:'^ *PACKAGESITE: $' \
	-o match:'^ *PKG_DBDIR: /var/db/pkg$' \
	-o match:'^ *PKG_CACHEDIR: /var/cache/pkg$' \
	-o match:'^ *PORTSDIR: /usr/ports$' \
	-o match:'^ *PUBKEY: $' \
	-o match:'^ *HANDLE_RC_SCRIPTS: no$' \
	-o match:'^ *ASSUME_ALWAYS_YES: no$' \
	-o match:'^ *PLIST_KEYWORDS_DIR: $' \
	-o match:'^ *SYSLOG: yes$' \
	-o match:'^ *AUTODEPS: no$' \
	-o match:'^ *ABI: [a-zA-Z0-9]+:[a-zA-Z0-9]+:[a-zA-Z0-9]+:[a-zA-Z0-9]+$' \
	-o match:'^ *DEVELOPER_MODE: no$' \
	-o match:'^ *PORTAUDIT_SITE: http://portaudit.FreeBSD.org/auditfile.tbz$' \
	-o match:'^ *VULNXML_SITE: http://www.vuxml.org/freebsd/vuln.xml.bz2$' \
	-o match:'^ *MIRROR_TYPE: SRV$' \
	-o match:'^ *FETCH_RETRY: 3$' \
	-o match:'^ *PKG_PLUGINS_DIR: /usr/local/lib/pkg/$' \
	-o match:'^ *PKG_ENABLE_PLUGINS: yes$' \
	-o match:'^ *PLUGINS:$' \
	-o match:'^ *DEBUG_SCRIPTS: no$' \
	-o match:'^ *PLUGINS_CONF_DIR: /usr/local/etc/pkg/$' \
	-o match:'^ *PERMISSIVE: no$' \
	-o match:'^ *REPO_AUTOUPDATE: yes$' \
	-o match:'^ *NAMESERVER: $' \
	-o match:'^ *EVENT_PIPE: $' \
	-o match:'^ *FETCH_TIMEOUT: 30$' \
	-o match:'^ *UNSET_TIMESTAMP: no$' \
	-o match:'^ *SSH_RESTRICT_DIR: $' \
	-o match:'^ *REPOS_DIR: /usr/local/etc/pkg/repos/$' \
	-o match:'^ *PKG_ENV:$' \
	-o match:'^ *DISABLE_MTREE: no$' \
	-e empty              \
	-s exit:0             \
	env -i PATH=${PATH} LD_LIBRARY_PATH=${LD_LIBRARY_PATH} pkg -C "" -vv
}


atf_init_test_cases() {
        . $(atf_get_srcdir)/test_environment

	atf_add_test_case pkg_no_database
	atf_add_test_case pkg_version
	atf_add_test_case pkg_config_defaults
}
