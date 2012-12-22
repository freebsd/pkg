TESTS=	version pkg
all:
	@echo "nothing todo"
run:
.for test in ${TESTS}
	@env LD_LIBRARY_PATH=${.CURDIR}/../../libpkg PATH=${.CURDIR}/../../pkg:${PATH} atf-sh ${test}.sh ${test}
.endfor
