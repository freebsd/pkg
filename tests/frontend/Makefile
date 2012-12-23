TESTS=	version pkg search
all:
	@echo "nothing todo"
run:
.for test in ${TESTS}
	@env PATH=${.CURDIR}/../../pkg:${PATH} atf-sh ${test}.sh ${test}
.endfor
