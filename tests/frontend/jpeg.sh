#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	jpeg

jpeg_body() {
	atf_skip_on Darwin Irrelevant on OSX

	cc -shared -Wl,-soname=libjpeg.so.3 -o libjpeg.so.3
	ln -sf libjpeg.so.3 libjpeg.so
	cc -shared -Wl,-rpath=${TMPDIR} -L. -ljpeg -o deponjpeg.so
	cc -shared -Wl,-rpath=${TMPDIR} -L. -ljpeg -o magicdeponjpeg.so

	cat << EOF > jpeg.ucl
name: jpeg
origin: graphics/jpeg
version: "1.0"
maintainer: test
categories: [test]
comment: jpeg
www: http://jpeg
prefix: /usr/local
desc: jpeg desc
files: {
	${TMPDIR}/libjpeg.so: "",
	${TMPDIR}/libjpeg.so.3: "",
}
EOF

	cat << EOF > jpeg-turbo.ucl
name: jpeg-turbo
origin: graphics/jpeg-turbo
version: "1.0"
maintainer: test
categories: [test]
comment: jpeg
www: http://jpeg
prefix: /usr/local
desc: jpeg desc
shlib_provided: [ "libjpeg.so.6" ]
files: {
	${TMPDIR}/libjpeg.so: "",
	${TMPDIR}/libjpeg.so.6: "",
}
EOF

	cat << EOF > deponjpeg.ucl
name: deponjpeg
origin: graphics/deponjpeg
version: "1.0"
maintainer: test
categories: [test]
comment: jpeg
www: http://jpeg
prefix: /usr/local
desc: jpeg desc
deps: {
	jpeg: { origin: graphics/jpeg, version: "1.0" }
}
files: {
	${TMPDIR}/deponjpeg.so: "",
}
EOF

	cat << EOF > deponjpeg2.ucl
name: deponjpeg
origin: graphics/deponjpeg
version: "1.0"
maintainer: test
categories: [test]
comment: jpeg
www: http://jpeg
prefix: /usr/local
desc: jpeg desc
deps: {
	jpeg-turbo: { origin: graphics/jpeg-turbo, version: "1.0" }
}
files: {
	${TMPDIR}/deponjpeg.so: "",
}
EOF

	cat << EOF > magicdeponjpeg.ucl
name: magicdeponjpeg
origin: graphics/magicdeponjpeg
version: "1.0"
maintainer: test
categories: [test]
comment: jpeg
www: http://jpeg
prefix: /usr/local
desc: jpeg desc
files: {
	${TMPDIR}/magicdeponjpeg.so: "",
}
EOF


	cat << EOF > magicdeponjpeg2.ucl
name: magicdeponjpeg
origin: graphics/magicdeponjpeg
version: "1.0"
maintainer: test
categories: [test]
comment: jpeg
www: http://jpeg
prefix: /usr/local
desc: jpeg desc
files: {
	${TMPDIR}/magicdeponjpeg.so: "",
}
EOF

	atf_check -o ignore -e empty pkg register -M jpeg.ucl
	atf_check -o ignore -e empty pkg register -M deponjpeg.ucl
	atf_check -o ignore -e empty pkg register -M magicdeponjpeg.ucl

	cc -shared -Wl,-soname=libjpeg.so.6 -o libjpeg.so.6
	ln -sf libjpeg.so.6 libjpeg.so
	cc -shared -Wl,-rpath=${TMPDIR} -L. -ljpeg -o deponjpeg.so
	cc -shared -Wl,-rpath=${TMPDIR} -L. -ljpeg -o magicdeponjpeg.so

	for p in jpeg deponjpeg2 magicdeponjpeg2 jpeg-turbo; do
		atf_check -o ignore \
			-e empty \
			pkg create -M ./${p}.ucl
	done

	atf_check -o ignore pkg repo .

	cat << EOF > repo.conf
local: {
	url: file://${TMPDIR}/,
	enabled: true
}
EOF
	atf_check \
		-o ignore \
		-e match:".*load error: access repo file.*" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" upgrade -y
}
