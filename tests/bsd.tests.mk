#	from: @(#)bsd.prog.mk	5.26 (Berkeley) 6/25/91
# $FreeBSD$

.include <bsd.init.mk>

.SUFFIXES: .out .o .c .cc .cpp .cxx .C .m .y .l .ln .s .S .asm

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

.  if ${MK_CTF} != "no" && ${DEBUG_FLAGS:M-g} != ""
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

.if defined(PROG_CXX)
PROG=	${PROG_CXX}
.endif

# TESTS -- a list of test binaries to generate.  We assume that for
# each test, there is a single source file test.c or test.cc. TESTS
# don't have manpages.  TESTS don't get installed outside the source
# tree.

.if defined(PROG)
.  if defined(SRCS)

OBJS+=  ${SRCS:N*.h:R:S/$/.o/g}

.    if target(beforelinking)
${PROG}: ${OBJS} beforelinking
.    else
${PROG}: ${OBJS}
.    endif
.    if defined(PROG_CXX)
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o ${.TARGET} ${OBJS} ${LDADD}
.    else
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${OBJS} ${LDADD}
.    endif
.    if ${MK_CTF} != "no"
	${CTFMERGE} ${CTFFLAGS} -o ${.TARGET} ${OBJS}
.    endif

.  else				# !defined(SRCS)

.    if !target(${PROG})
.      if defined(PROG_CXX)
SRCS=	${PROG}.cc
.      else
SRCS=	${PROG}.c
.      endif

# Always make an intermediate object file because:
# - it saves time rebuilding when only the library has changed
# - the name of the object gets put into the executable symbol table instead of
#   the name of a variable temporary object.
# - it's useful to keep objects around for crunching.
OBJS=	${PROG}.o

.      if target(beforelinking)
${PROG}: ${OBJS} beforelinking
.      else
${PROG}: ${OBJS}
.      endif
.      if defined(PROG_CXX)
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o ${.TARGET} ${OBJS} ${LDADD}
.      else
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${OBJS} ${LDADD}
.      endif
.      if ${MK_CTF} != "no"
	${CTFMERGE} ${CTFFLAGS} -o ${.TARGET} ${OBJS}
.      endif
.    endif

.  endif			# !defined(SRCS)

all: objwarn ${PROG} ${SCRIPTS}

.  if defined(PROG)
CLEANFILES+= ${PROG}
.  endif

.  if defined(OBJS)
CLEANFILES+= ${OBJS}
.  endif

.include <bsd.libnames.mk>

.  if defined(PROG)
_EXTRADEPEND:
.  if defined(LDFLAGS) && !empty(LDFLAGS:M-nostdlib)
.    if defined(DPADD) && !empty(DPADD)
	echo ${PROG}: ${DPADD} >> ${DEPENDFILE}
.    endif
.  else
	echo ${PROG}: ${LIBC} ${DPADD} >> ${DEPENDFILE}
.    if defined(PROG_CXX)
.      if !empty(CXXFLAGS:M-stdlib=libc++)
	echo ${PROG}: ${LIBCPLUSPLUS} >> ${DEPENDFILE}
.      else
	echo ${PROG}: ${LIBSTDCPLUSPLUS} >> ${DEPENDFILE}
.      endif
.    endif
.  endif
.endif				# defined(PROG)

.if !target(lint)
lint: ${SRCS:M*.c}
.  if defined(PROG)
	${LINT} ${LINTFLAGS} ${CFLAGS:M-[DIU]*} ${.ALLSRC}
.  endif
.endif

.include <bsd.dep.mk>

.if defined(PROG) && !exists(${.OBJDIR}/${DEPENDFILE})
${OBJS}: ${SRCS:M*.h}
.endif

.include <bsd.obj.mk>

.include <bsd.sys.mk>
