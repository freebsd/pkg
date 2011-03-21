PKGPREINSTALL?=		${PKGDIR}/pkg-pre-install
PKGPOSTINSTALL?=	${PKGDIR}/pkg-post-install
PKGPREDEINSTALL?=	${PKGDIR}/pkg-pre-deinstall
PKGPOSTDEINSTALL?=	${PKGDIR}/pkg-post-deinstall
PKGPREUPGRADE?=		${PKGDIR}/pkg-pre-upgrade
PKGPOSTUPGRADE?=	${PKGDIR}/pkg-post-upgrade
PKGUPGRADE?=		${PKGDIR}/pkg-upgrade

ACTUAL-PACKAGE-DEPENDS?= \
	if [ "${_LIB_RUN_DEPENDS}" != "  " ]; then \
		for dir in ${_LIB_RUN_DEPENDS:C,[^:]*:([^:]*):?.*,\1,}; do \
			pkgname=$$(${PKG_INFO} -q $${dir\#\#${PORTSDIR}/}); \
			${ECHO_CMD} $$pkgname:$${dir\#\#${PORTSDIR}/}; \
			for pkg in $$(${PKG_INFO} -qd $${dir\#\#${PORTSDIR}/}); do\
				origin=$$(${PKG_INFO} -qo $${pkg%-*}); \
				${ECHO_CMD} $$pkg:$$origin; \
			done; \
		done; \
	fi

.if !defined(PKG_ARGS)
PKG_ARGS=		-v -c -${COMMENT:Q} -d ${DESCR} -f ${TMPPLIST} -p ${PREFIX} -P "`cd ${.CURDIR} && ${MAKE} actual-package-depends | ${GREP} -v -E ${PKG_IGNORE_DEPENDS} | ${SORT} -u -t : -k 2`" ${EXTRA_PKG_ARGS} $${_LATE_PKG_ARGS}
.if !defined(NO_MTREE)
PKG_ARGS+=		-m ${MTREE_FILE}
.endif
.if defined(PKGORIGIN)
PKG_ARGS+=		-o ${PKGORIGIN}
.endif
.if defined(CONFLICTS) && !defined(DISABLE_CONFLICTS)
PKG_ARGS+=		-C "${CONFLICTS}"
.endif
.if defined(CONFLICTS_INSTALL) && !defined(DISABLE_CONFLICTS)
PKG_ARGS+=		-C "${CONFLICTS_INSTALL}"
.endif
PKG_ARGS+= -n ${PKGNAME}
.if defined(MAINTAINER)
PKG_ARGS+= -r ${MAINTAINER}
.endif
.if defined(WWW)
PKG_ARGS+= -w ${WWW}
.endif
.if exists(${PKGMESSAGE})
PKG_ARGS+= -M ${PKGMESSAGE}
.endif
.if exists(${PKGINSTALL})
PKGSCRIPTS+=	${PKGINSTALL}
.endif
.if exists(${PKGPREINSTALL})
PKGSCRIPTS+=	${PKGPREINSTALL}
.endif
.if exists(${PKGPOSTINSTALL})
PKGSCRIPTS+=	${PKGPOSTINSTALL}
.endif
.if exists(${PKGDEINSTALL})
PKGSCRIPTS+=	${PKGDEINSTALL}
.endif
.if exists(${PKGPREDEINSTALL})
PKGSCRIPTS+=	${PKGPREDEINSTALL}
.endif
.if exists(${PKGPOSTDEINSTALL})
PKGSCRIPTS+=	${PKGPOSTDEINSTALL}
.endif
.if exists(${PKGUPGRADE})
PKGSCRIPTS+=	${PKGUPGRADE}
.endif
.if exists(${PKGPREUPGRADE})
PKGSCRIPTS+=	${PKGPREUPGRADE}
.endif
.if exists(${PKGPOSTUPGRADE})
PKGSCRIPTS+=	${PKGPOSTUPGRADE}
.endif
.if defined(PKGSCRIPTS)
PKG_ARGS+=	-s "${PKGSCRIPTS}"
.endif
.endif

.if !target(fake-pkg)
fake-pkg:
.if !defined(NO_PKG_REGISTER)
	@${ECHO_MSG} "===>   Registering installation for ${PKGNAME}"
.if defined(FORCE_PKG_REGISTER)
	@${PKG_CMD} ${PKG_ARGS} -F
.else
	@${PKG_CMD} ${PKG_ARGS}
.endif
.else
	@${DO_NADA}
.endif
.endif
