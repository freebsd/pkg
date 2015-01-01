m4_define([maj_ver], [1])
m4_define([med_ver], [4])
m4_define([min_ver], [99])
m4_define([api_ver], [m4_eval(maj_ver * 1000000 + med_ver * 1000 + min_ver)])
m4_define([dev_ver], m4_expand([m4_normalize(m4_sinclude([dev_version.m4]))]))
m4_ifset([dev_ver],
       [m4_define([pkg_version], [maj_ver.med_ver.min_ver.dev_ver])],
       [m4_define([pkg_version], [maj_ver.med_ver.min_ver])])

AC_INIT([pkg],m4_expand([pkg_version]),[https://github.com/freebsd/pkg],[pkg])
AC_CONFIG_SRCDIR([configure.ac])
AM_INIT_AUTOMAKE([1.11 foreign -Wno-portability no-dist-gzip dist-xz])
AM_SILENT_RULES([yes])

AM_MAINTAINER_MODE([enable])

dnl Here are rules for setting version-info for libpkg
dnl we use 3 integers: current, revision and age:
dnl - if you change ABI (meaning add/remove/modify), then always update `current`
dnl - if you modify lib (that means ALL modifies), then increase `revision`
dnl - if you want backward compatibility, then use `age`
LIBPKG_CURRENT=3
LIBPKG_REVISION=0
LIBPKG_AGE=0
LIBPKG_SO_VERSION="$LIBPKG_CURRENT:$LIBPKG_REVISION:$LIBPKG_AGE"
AC_SUBST(LIBPKG_SO_VERSION)

AC_GNU_SOURCE
AC_PROG_CC_C99
LT_INIT()
AC_CONFIG_MACRO_DIR([m4])
AX_CFLAGS_WARN_ALL

AC_C_CONST
AC_TYPE_SIZE_T
AC_CHECK_FUNCS(memmove bcopy)
AC_PROG_LN_S

AC_CHECK_HEADERS_ONCE([fcntl.h unistd.h elf-hints.h])
AC_TYPE_OFF_T
AC_FUNC_MMAP

if test "$ac_cv_func_mmap_fixed_mapped" = "yes"; then
    FILEMAP=unixfilemap
else
    FILEMAP=readfilemap
fi
AC_SUBST(FILEMAP)

AC_MSG_CHECKING(for GCC atomic builtins)
AC_LINK_IFELSE([
	AC_LANG_SOURCE([[
		int main() {
			volatile unsigned long val = 1;
			__sync_synchronize();
			__sync_val_compare_and_swap(&val, 1, 0);
			__sync_add_and_fetch(&val, 1);
			__sync_sub_and_fetch(&val, 1);
			return 0;
		}
	]])
],
[
 	AC_MSG_RESULT([yes])
	AC_DEFINE([HAVE_ATOMIC_BUILTINS], [1], [Has gcc/MSVC atomic intrinsics])
],
[
 	AC_MSG_RESULT([no])
])

gl_LD_VERSION_SCRIPT

AC_CANONICAL_HOST
case $host_os in
  linux*)
        OS_CFLAGS="-D_XOPEN_SOURCE -D_GNU_SOURCE -pthread"
        OS_LDFLAGS="-pthread"
        OS_LIBS="-ldl -lrt"
        OS_STATIC="false"
        ;;
    *)
        OS_CFLAGS="-D_BSD_SOURCE"
        OS_LDFLAGS=
        OS_LIBS=
        OS_STATIC="true"
        ;;
esac

AC_SUBST(OS_CFLAGS)
AC_SUBST(OS_LDFLAGS)
AC_SUBST(OS_LIBS)
AC_SUBST(OS_STATIC)
AM_CONDITIONAL([BUILD_STATIC], [test x$OS_STATIC = xtrue])

CFLAGS="$CFLAGS $OS_CFLAGS"

AC_CHECK_HEADERS_ONCE([machine/endian.h])
AC_CHECK_HEADERS_ONCE([endian.h])
AC_CHECK_HEADERS_ONCE([sys/endian.h])
AC_CHECK_HEADERS_ONCE([fcntl.h])
AC_CHECK_HEADERS_ONCE([dlfcn.h])
AC_CHECK_HEADERS_ONCE([memory.h])
AC_CHECK_HEADERS_ONCE([sys/types.h])
AC_CHECK_HEADERS_ONCE([sys/stat.h])
AC_CHECK_HEADERS_ONCE([stdlib.h])
AC_CHECK_HEADERS_ONCE([string.h])
AC_CHECK_HEADERS_ONCE([memory.h])
AC_CHECK_HEADERS_ONCE([inttypes.h])
AC_CHECK_HEADERS_ONCE([unistd.h])
AC_CHECK_HEADERS_ONCE([dlfcn.h])
AC_CHECK_HEADERS_ONCE([fcntl.h])

AC_CHECK_HEADERS_ONCE([sys/types.h])
AC_CHECK_HEADERS_ONCE([sys/stat.h])
AC_CHECK_HEADERS_ONCE([sys/param.h])
AC_CHECK_HEADERS_ONCE([sys/mman.h])
AC_CHECK_HEADERS_ONCE([sys/elf_common.h])
AC_CHECK_HEADERS_ONCE([link.h])
AC_CHECK_HEADERS_ONCE([stdlib.h])
AC_CHECK_HEADERS_ONCE([stddef.h])
AC_CHECK_HEADERS_ONCE([stdarg.h])
AC_CHECK_HEADERS_ONCE([stdbool.h])
AC_CHECK_HEADERS_ONCE([stdint.h])
AC_CHECK_HEADERS_ONCE([string.h])
AC_CHECK_HEADERS_ONCE([unistd.h])
AC_CHECK_HEADERS_ONCE([ctype.h])
AC_CHECK_HEADERS_ONCE([errno.h])
AC_CHECK_HEADERS_ONCE([limits.h])
AC_CHECK_HEADERS_ONCE([libgen.h])
AC_CHECK_HEADERS_ONCE([stdio.h])
AC_CHECK_HEADERS_ONCE([float.h])
AC_CHECK_HEADERS_ONCE([math.h])
AC_CHECK_HEADERS_ONCE([osreldate.h])
AC_CHECK_HEADERS_ONCE([sys/socket.h])
AC_CHECK_HEADERS_ONCE([netinet/in.h])
AC_CHECK_HEADERS_ONCE([netinet/in6.h])
AC_CHECK_HEADERS_ONCE([sys/statfs.h])
AC_CHECK_HEADERS_ONCE([dirent.h], [sys/ndir.h], [sys/dir.h], [ndir.h])
AC_CHECK_HEADERS_ONCE([sys/capability.h])
AC_CHECK_HEADERS_ONCE([sys/capsicum.h])
AC_CHECK_HEADERS_ONCE([bsd/stdlib.h])
AC_CHECK_HEADERS_ONCE([bsd/string.h])
AC_CHECK_HEADERS_ONCE([bsd/stdio.h])
AC_CHECK_HEADERS_ONCE([bsd/readpassphrase.h])
AC_CHECK_HEADERS_ONCE([bsd/libutil.h])
AC_CHECK_HEADERS_ONCE([bsd/err.h])
AC_CHECK_HEADERS_ONCE([bsd/unistd.h])
AC_CHECK_HEADERS_ONCE([bsd/sys/cdefs.h])
AC_CHECK_HEADERS_ONCE([sys/procctl.h])

AC_CHECK_HEADER([regex.h], [
	AC_DEFINE(HAVE_REGEX_H, 1, [Define to 1 if you have the <regex.h> header file.])
	],
	[AC_MSG_ERROR([unable to find the regex.h header])],
	[#include <sys/types.h>])

AC_CHECK_FUNCS_ONCE([posix_fallocate])
AC_CHECK_FUNCS_ONCE([usleep])
AC_CHECK_FUNCS_ONCE([localtime_r])
AC_CHECK_FUNCS_ONCE([gmtime_r])
AC_CHECK_FUNCS_ONCE([strerror_r])
AC_CHECK_FUNCS_ONCE([strtonum])
AC_CHECK_FUNCS_ONCE([strnstr])
AC_CHECK_FUNCS_ONCE([funopen])
AC_CHECK_FUNCS_ONCE([fopencookie])
AC_CHECK_FUNCS_ONCE([sysctlbyname])
AC_CHECK_DECLS(
	[[fstatat], [openat], [unlinkat], [readlinkat], [faccessat]],
	[], [], [
		#include <sys/types.h>
		#include <fcntl.h>
		#include <sys/stat.h>
		#include <unistd.h>
	])
AC_CHECK_DECLS(
	[[be16dec], [be32dec], [be64dec], [le16dec], [le32dec], [le64dec],
	 [be16enc], [be32enc], [be64enc], [le16enc], [le32enc], [le64enc]],
	[], [], [#include <sys/endian.h>]
)
AC_CHECK_FUNCS_ONCE([flopen])
AC_CHECK_FUNCS_ONCE([eaccess])
AC_CHECK_FUNCS_ONCE([closefrom])
AC_CHECK_FUNCS_ONCE([dirfd])
AC_CHECK_FUNCS_ONCE([sysconf])

AC_CHECK_MEMBERS([struct in6_addr.s6_addr32, 
	struct in6_addr.s6_addr16, 
	struct sockaddr_in.sin_len, 
	struct sockaddr_in6.sin6_len, 
	struct sockaddr_storage.ss_len,
	struct sockaddr_storage.ss_family,
	struct sockaddr_storage.__ss_family], [], [],
[#include <sys/types.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN6_H
#include <netinet/in6.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
])

PKG_PROG_PKG_CONFIG

AC_DEFUN([AC_PROG_GIT], [AC_CHECK_PROG(GITBIN,git,yes)])
AC_PROG_GIT
if test x"${GITBIN}" == x"yes" ; then
	AC_MSG_CHECKING([for git head])
	if test -d "${GIT_DIR:-${ac_top_srcdir:-./}/.git}" ; then
		GIT_HEAD=`git rev-parse --short HEAD`
		if test -n "`git diff-index -m --name-only HEAD`" ; then
			GIT_HEAD=${GIT_HEAD}-dirty
		fi
		if test -n "${GIT_HEAD}" ; then
			GIT_HEAD="-${GIT_HEAD}"
		fi
	fi
fi
AH_TEMPLATE(GIT_HEAD, "Git revision")
AH_TEMPLATE(PKG_API, "Api version")
AC_MSG_RESULT([$GIT_HEAD])
AC_SUBST([GIT_HEAD])
AC_DEFINE_UNQUOTED([GIT_HEAD], $GIT_HEAD)
AC_DEFINE([PKG_API], [api_ver])

AC_RUN_IFELSE(
  [AC_LANG_SOURCE(
    [[
    #include <sys/types.h>
    #include <sys/socket.h>
    int main() { return socket(AF_LOCAL, SOCK_SEQPACKET, 0) == -1 ? -1 : 0; }
    ]]
  )],
  [AC_DEFINE([HAVE_SEQPACKET], 1, [Define to 1 if you have SOCK_SEQPACKET working])]
)

AC_SUBST([HAVE_SEQPACKET])

AC_SEARCH_LIBS([lzma_version_string], [lzma], [], [
  AC_MSG_ERROR([unable to find the liblzma])
])
AC_SEARCH_LIBS([BZ2_bzReadOpen], [bz2], [], [
  AC_MSG_ERROR([unable to find the libbz2])
])
AC_SEARCH_LIBS([zlibVersion], [z], [], [
  AC_MSG_ERROR([unable to find the zlib])
])
AC_SEARCH_LIBS([archive_read_open], [archive], [], [
  AC_MSG_ERROR([unable to find the archive_read() function])
])

AC_CHECK_HEADER([archive.h],
	[pkg_found_archive_headers=yes])
AS_IF([test "x$pkg_found_archive_headers" != "xyes"],
	[AC_MSG_ERROR([Unable to find the libarchive headers])])

AC_SEARCH_LIBS([jail_getid], [jail], [
	AC_DEFINE(HAVE_LIBJAIL, 1, [Define to 1 if you have the 'jail' library (-ljail).])
	LIBJAIL_LIB="-ljail"
	], [])
	
AC_SEARCH_LIBS([getprogname], [bsd], [
	AC_DEFINE(HAVE_LIBBSD, 1, [Define to 1 if you have the 'bsd' library (-lbsd).])
	LIBBSD_LIB="-lbsd"
	], [])

AC_CHECK_HEADERS([gelf.h libelf.h], [
	AC_CHECK_TYPES([Elf_Note], [
		AC_DEFINE(HAVE_GELF_H, 1, [Define to 1 if you have the <gelf.h> header file.])
		AC_CHECK_LIB(elf, gelf_getehdr, [
			AC_DEFINE(HAVE_LIBELF, 1, [Define to 1 if you have the 'elf' library (-lelf).])
			libelf_bundled=no
		]) dnl AC_CHECK_LIB
	], dnl AC_CHECK_TYPES
	[], AC_LANG_SOURCE(
	[[
#include "gelf.h"
#include "libelf.h"
	]])
	)])

AC_ARG_WITH([ldns], AS_HELP_STRING([--with-ldns], [Build with ldns for name resolving]))

AS_IF([test "x$with_ldns" = "xyes"], [
   PKG_CHECK_MODULES([LDNS], [libldns], [
   	AC_DEFINE([HAVE_LDNS], [1], [Use ldns])
   	AC_SUBST([HAVE_LDNS], [1], [Use ldns])
   ],
   [
   	AC_CHECK_HEADER([ldns/ldns.h], [
   		AC_CHECK_LIB(ldns, ldns_resolver_new_frm_file, [
   			AC_DEFINE([HAVE_LDNS], [1], [Use ldns])
   			AC_SUBST([HAVE_LDNS], [1], [Use ldns])
   			AC_SUBST([LDNS_LIBS], [-lldns], [LDNS library])
   		])
   	])
   ])
   AS_IF([test "x$HAVE_LDNS" != "x1"],
	[AC_MSG_ERROR([Unable to find the libdns])])
])

AC_CHECK_LIB(c, cap_sandboxed, [
   AC_DEFINE(HAVE_CAPSICUM, 1, [Define 1 if you have 'capsicum'.])
])


AC_CACHE_CHECK(for arc4random_uniform,
               ac_cv_func_arc4random_uniform,
               [AC_TRY_COMPILE([#include <stdlib.h>],
                               [arc4random_uniform(255);],
                               [ac_cv_func_arc4random_uniform=yes],
                               [ac_cv_func_arc4random_uniform=no])])
if test "$ac_cv_func_arc4random_uniform" = yes ; then
	AC_DEFINE(HAVE_ARC4RANDOM, 1, [Define 1 if you have 'arc4random_uniform' function.])
fi

AC_CACHE_CHECK(for BSD dirname(const char *),
               ac_cv_func_dirname,
               [ac_save_CFLAGS="$CFLAGS"
		CFLAGS="-Werror $CFLAGS"
		AC_TRY_COMPILE([#include <libgen.h>],
                               [static char *(*ac_test_dirname)(const char *) = dirname; ac_test_dirname("");],
                               [ac_cv_func_dirname=yes],
                               [ac_cv_func_dirname=no])
		CFLAGS="$ac_save_CFLAGS"])
if test "$ac_cv_func_dirname" = yes ; then
	AC_DEFINE(HAVE_BSD_DIRNAME, 1, [Define 1 if you have 'dirname(const char *)' function.])
fi

AC_CHECK_FUNCS(basename_r)
AC_CACHE_CHECK(for BSD basename(const char *),
               ac_cv_func_basename,
               [ac_save_CFLAGS="$CFLAGS"
		CFLAGS="-Werror $CFLAGS"
		AC_TRY_COMPILE([#include <libgen.h>],
                               [static char *(*ac_test_basename)(const char *) = basename; ac_test_basename("");],
                               [ac_cv_func_basename=yes],
                               [ac_cv_func_basename=no])
		CFLAGS="$ac_save_CFLAGS"])
if test "$ac_cv_func_basename" = yes ; then
	AC_DEFINE(HAVE_BSD_BASENAME, 1, [Define 1 if you have 'basename(const char *)' function.])
fi

AC_MSG_CHECKING([for /proc/pid/fd directory])
if test -d "/proc/$$/fd" ; then
        AC_DEFINE([HAVE_PROC_PID], [1], [Define if you have /proc/$pid/fd])
        AC_MSG_RESULT([yes])
else
        AC_MSG_RESULT([no])
fi

AC_CHECK_DECL([F_CLOSEM],
        AC_DEFINE([HAVE_FCNTL_CLOSEM], [1], [Use F_CLOSEM fcntl for closefrom]),
        [],
        [ #include <limits.h>
          #include <fcntl.h> ]
)

AC_MSG_CHECKING([whether we should only build the static version of pkg])
AC_ARG_WITH([staticonly],
	      [AS_HELP_STRING([--with-staticonly],
			      [Only build the static version (default is no)])])

if test "$with_staticonly" = "yes"; then
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi

AM_CONDITIONAL(DYNAMIC, test "$with_staticonly" != "no")

dnl Select appropriate ABI
AS_CASE(
	[$host_os],
	[darwin*],
	[ac_cv_binary_abi=macho],
	[ac_cv_binary_abi=elf]
)
AM_CONDITIONAL(HAVE_MACHO_ABI, test "$ac_cv_binary_abi" = "macho")
AM_CONDITIONAL(HAVE_ELF_ABI, test "$ac_cv_binary_abi" = "elf")
AM_CONDITIONAL(LIBELF_BUNDLED, test "$ac_cv_binary_abi" = "elf" -a "$libelf_bundled" != "no")

AC_SUBST([LIBJAIL_LIB])
AC_SUBST([LIBBSD_LIB])
AC_SUBST([TESTS])
AC_SUBST([LDNS_LIBS])
AC_SUBST([LDNS_CFLAGS])

dnl We now support merely binary repos
m4_define([repos], [binary])
m4_define([repos_ldadd], [])
m4_define([repos_makefiles], [])
m4_define([repos_ldadd_static], [])

dnl Add ldadd for repo
m4_define([repos_add_ldadd], [
	m4_append([$1], [m4_join([], [$(top_builddir)/libpkg/repo/], 
		[$3], [/librepo_], 
		[$3], [$2.la ])])
])
dnl Create makefile definition for repo
m4_define([repos_add_makefile], [
	m4_append([$1], [m4_join([], [\], m4_newline(), [libpkg/repo/], 
		[$2], [/Makefile ])])
])
dnl Apply macros to repos list
m4_map([m4_curry([repos_add_ldadd], [repos_ldadd], [])], [repos])
m4_map([m4_curry([repos_add_ldadd], [repos_ldadd_static], [_static])], [repos])
m4_map([m4_curry([repos_add_makefile], [repos_makefiles])], [repos])

REPOS="m4_expand([repos])"
REPOS_LDADD='m4_normalize(m4_expand([repos_ldadd]))'
REPOS_LDADD_STATIC='m4_normalize(m4_expand([repos_ldadd_static]))'
AC_SUBST([REPOS])
AC_SUBST([REPOS_LDADD])
AC_SUBST([REPOS_LDADD_STATIC])

AC_CONFIG_FILES(Makefile \
		src/Makefile \
		external/Makefile \
		external/blake2/Makefile \
		scripts/Makefile \
		docs/Makefile \
		tests/Makefile \
		compat/Makefile \
		libpkg/Makefile \
		libpkg/repo/Makefile \
		libpkg/pkg.h \
		libpkg/pkg.pc
		scripts/periodic/400.status-pkg
		scripts/periodic/410.pkg-audit
		scripts/periodic/411.pkg-backup
		scripts/periodic/460.pkg-checksum
		scripts/periodic/490.status-pkg-changes
		scripts/sbin/pkg2ng
		scripts/completion/_pkg.bash
		scripts/completion/_pkg)
AC_CONFIG_FILES(m4_expand([repos_makefiles]))

AC_CONFIG_HEADERS(pkg_config.h)
AC_CONFIG_COMMANDS([pkg_repos.h], [[
	NMODULES=pkg_repos.h.new
	echo "#ifndef PKG_REPOS_H" > $NMODULES
	echo "#define PKG_REPOS_H" >> $NMODULES
	for rt in $REPOS ; do
		echo "extern struct pkg_repo_ops pkg_repo_${rt}_ops;" >> $NMODULES;
	done
	echo "struct pkg_repo_ops* repos_ops[] = {" >> $NMODULES
	for rt in $REPOS ; do
		echo "&pkg_repo_${rt}_ops," >> $NMODULES
	done
	echo "NULL" >> $NMODULES
	echo "};" >> $NMODULES
	echo "#endif /* PKG_REPOS_H */" >> $NMODULES
	if [ -f pkg_repos.h ] ; then
		diff $NMODULES pkg_repos.h > /dev/null 2>&1
		if [ $? -ne 0 ] ; then
			cp $NMODULES pkg_repos.h
		fi
	else
		cp $NMODULES pkg_repos.h
	fi
	rm $NMODULES
]], [REPOS=$REPOS])

AC_CONFIG_COMMANDS([mkdir], [$MKDIR_P tests/frontend])

AC_OUTPUT
