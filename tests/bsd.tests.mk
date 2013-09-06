#	from: @(#)bsd.prog.mk	5.26 (Berkeley) 6/25/91
# $FreeBSD$

.include <bsd.init.mk>

.SUFFIXES: .out .o .c .cc .cpp .cxx .C .m .y .l .ln .s .S .asm

# libatf is not in base for stable/9 or earlier
CFLAGS+=	-I. -I/usr/local/include
LDADD+=		-L/usr/local/lib
LATF_C=		-latf-c
LATF_CXX=	-latf-c++

# XXX The use of COPTS in modern makefiles is discouraged.
.if defined(COPTS)
CFLAGS+=${COPTS}
.endif

.if ${MK_ASSERT_DEBUG} == "no"
CFLAGS+= -DNDEBUG
NO_WERROR=
.endif

.if defined(DEBUG_FLAGS)
CFLAGS+=${DEBUG_FLAGS}
CXXFLAGS+=${DEBUG_FLAGS}

.  if defined(MK_CTF) && ${MK_CTF} != "no" && ${DEBUG_FLAGS:M-g} != ""
CTFFLAGS+= -g
.  endif
.endif

.if defined(CRUNCH_CFLAGS)
CFLAGS+=${CRUNCH_CFLAGS}
.endif

.if !defined(DEBUG_FLAGS)
STRIP?=	-s
.endif

.if defined(NO_SHARED) && (${NO_SHARED} != "no" && ${NO_SHARED} != "NO")
LDFLAGS+= -static
.endif

.if defined(TESTS_CXX)
TESTS+=	${TESTS_CXX}
.endif

# TESTS -- a list of test binaries to generate.  We assume that for
# each test 'foo', there is at minimum a single primary source file
# foo.c
#
# TESTS_CXX -- all tests that use c++ source code.  Assume the primary
# source file is foo.cc
#
# SRCS -- a list of source files needed for every test in ${TESTS}
#
# foo_SRCS -- a list of (additional) source files needed specifically
# for test foo.  If foo.c is not mentioned in this list, it will be
# automatically prepended to it.
#
# eg. Tests 'foo', 'bar' and 'baz'.  foo and bar are compiled
# respectively from their primary source code foo.c or bar.c, and a
# secondary file foobar_extra.c (specific to test foo and test bar)
# and common.c.  bar.c is implicitly prepended to ${bar_SRCS}. Test
# 'baz' is compiled from baz.c and common.c.  All tests also depend on
# tests.h.
#
#   TESTS=	foo bar baz
#   foo_SRCS=	foo.c foobar_extra.c
#   bar_SRCS=	foobar_extra.c
#   SRCS=	common.c tests.h
#
# TESTS don't have manpages.  TESTS don't get installed anywhere, but
# are run directly from the source tree.

.for _test in ${TESTS}

.  if defined(TESTS_CXX:M${_test})
${_test}_SRCS:=	${_test}.cc ${${_test}_SRCS:N${_test}.cc} ${SRCS}
.  else
${_test}_SRCS:=	${_test}.c ${${_test}_SRCS:N${_test}.c} ${SRCS}
.  endif

.  if defined(${_test}_SRCS)
_SRCS+=	${${_test}_SRCS}
${_test}_OBJS=	${${_test}_SRCS:N*.h:R:S/$/.o/g}
.  endif

.  if defined(${_test}_OBJS)
.    if target(beforelinking)
${_test}: ${${_test}_OBJS} beforelinking
.    else
${_test}: ${${_test}_OBJS}
.    endif
.    if defined(TESTS_CXX:M${_test})
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o ${.TARGET} ${${_test}_OBJS} ${LDADD} ${LATF_CXX}
.    else
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${${_test}_OBJS} ${LDADD} ${LATF_C}
.    endif
.    if defined(MK_CTF) && ${MK_CTF} != "no"
	${CTFMERGE} ${CTFFLAGS} -o ${.TARGET} ${${_test}_OBJS}
.    endif
.  endif			# defined(${_test}_OBJS)
.endfor				# for _test in ${TESTS}

ALLSRCS:=	${_SRCS:O:u}
OBJS=	${ALLSRCS:N*.h:R:O:u:S/$/.o/g}

.if defined(TESTS)
all: objwarn ${TESTS} ${SCRIPTS}

CLEANFILES+= ${TESTS}

.  if defined(OBJS)
CLEANFILES+= ${OBJS}
.  endif

.include <bsd.libnames.mk>

_EXTRADEPEND:
.  if defined(LDFLAGS) && !empty(LDFLAGS:M-nostdlib)
.    if defined(DPADD) && !empty(DPADD)
.      for _test in ${TESTS}
	echo ${_test}: ${DPADD} >> ${DEPENDFILE}
.      endfo
.    endif
.  else
.    for _test in ${TESTS}
	echo ${PROG}: ${LIBC} ${DPADD} >> ${DEPENDFILE}
.      if defined(TESTS_CXX:M${_test})
.        if !empty(CXXFLAGS:M-stdlib=libc++)
	echo ${_test}: ${LIBCPLUSPLUS} >> ${DEPENDFILE}
.        else
	echo ${_test}: ${LIBSTDCPLUSPLUS} >> ${DEPENDFILE}
.        endif
.      endif
.    endfor
.  endif
.endif				# defined(TESTS)

.if !target(lint)
lint: ${SRCS:M*.c}
.  if defined(TESTS)
	${LINT} ${LINTFLAGS} ${CFLAGS:M-[DIU]*} ${.ALLSRC}
.  endif
.endif

.include <bsd.dep.mk>

.if defined(TESTS) && !exists(${.OBJDIR}/${DEPENDFILE})
${OBJS}: ${SRCS:O:u:M*.h}
.endif

.include <bsd.obj.mk>

.include <bsd.sys.mk>
