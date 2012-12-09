
SUBDIR=	external \
	libpkg \
	pkg

SUBDIR_TARGETS=	set-version

NEWVERS=	newvers.sh

.if !defined(NOSTATIC)
SUBDIR+=	pkg-static
.endif

PKGVERSION!=    sh ${NEWVERS} pkg

# All the files modified by changing version strings.  Changes to
# these files will be commited to git unconditionally as part of the
# 'release' target.

VERSIONED_FILES=	Doxyfile libpkg/pkg.h ${NEWVERS}

# Set CREATE_SNAPSHOT=yes to create a snapshot, which will update
# Doxyfile, libpkg/pkg.h etc. but not newvers.sh.  To clear the
# snapshot, either update ${NEWVERS} or set CREATE_SNAPSHOT=no

.if defined(CREATE_SNAPSHOT)
_snapshot=	snapshot

${_snapshot}:
.endif

TARBALL_BASENAME=	pkg-${PKGVERSION}
TARBALL_EXT=		tar.xz
TARBALL_FILE=		${TARBALL_BASENAME}.${TARBALL_EXT}

.PHONY: release do-release set-tag make-tarball regression-test \
	set-version do-set-version ${_snapshot} 

all:	set-version

do-set-version:
	@${ECHO} "==> Update version strings (${PKGVERSION})" 

set-version: do-set-version Doxyfile

regression-test: clean all
	@${ECHO} "==> Regression Test"

do-release: regression-test
	@${ECHO} "==> Create New Release (${PKGVERSION})"

release: do-release set-tag make-tarball

set-tag:
	@if [ -n "$$( git status -uno -s )" ] ; then \
	    git commit -uno -m "New Release ${PKGVERSION}" ${VERSIONED_FILES} ; \
	fi
	@if git tag -l | grep -F ${PKGVERSION} ; then \
	    ${ECHO} "---> Error: tag ${PKGVERSION} already exists" ; \
	    ${ECHO} "---> Either delete the tag (git tag -d ${PKGVERSION})" ; \
	    ${ECHO} "---> (but only if you haven't pushed yet) or edit" ; \
	    ${ECHO} "---> ${NEWVERS} to set the new release version" ; \
	    false ; \
	fi
	git tag -m "New Release ${PKGVERSION}" ${PKGVERSION}


# Note: you will need to update ~/.gitconfig so git understands tar.xz
# as a format
make-tarball:
	git archive --format=${TARBALL_EXT} --prefix=${TARBALL_BASENAME}/ \
	    -o ${TARBALL_FILE} ${PKGVERSION}

Doxyfile: ${NEWVERS} ${_snapshot}
	sed -e "/^PROJECT_NUMBER/s,= .*,= ${PKGVERSION}," -i '' ${.TARGET}

.include <bsd.subdir.mk>
