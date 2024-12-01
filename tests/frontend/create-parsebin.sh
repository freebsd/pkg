#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	create_from_bin \
    create_from_binbase

genmanifest() {
    local PKG_NAME="$1"
    shift
    local PKG_FLATSIZE=0
    local PKG_FILES=""
    local PKG_SHA256=""
    local NL="
"

    bin_meta "$1"
    while [ -n "$1" ]; do
        local file1="${1%#*}"
        local file1_base=$(basename ${file1})
        local file1_size=$(wc -c < ${file1})
        local file1_sha=$(openssl dgst -sha256 -hex ${file1} | sed -nE 's/.*=[[:space:]]*([[:xdigit:]]+)/\1/p')
        cp -a ${file1} ${TMPDIR}/${file1_base}

        PKG_FILES="${PKG_FILES}/${file1_base}: {perm: 0644}${NL}"
        PKG_SHA256="${PKG_SHA256}${NL}    /${file1_base} = \"1\$${file1_sha}\";"

        PKG_FLATSIZE=$((${PKG_FLATSIZE}+${file1_size}))
        shift
    done

	cat << EOF > ${PKG_NAME}.manifest
name: ${PKG_NAME}
origin: ${PKG_NAME}
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /
desc: <<EOD
Yet another test
EOD
files: {
    ${PKG_FILES}
}
EOF

	cat << EOF > ${PKG_NAME}.expected
name = "${PKG_NAME}";
origin = "${PKG_NAME}";
version = "1";
comment = "a test";
maintainer = "test";
www = "http://test";
abi = "${XABI}";
arch = "${XALTABI}";
prefix = "/";
flatsize = ${PKG_FLATSIZE};
desc = "Yet another test";
categories [
    "test",
]
EOF
    if [ x"${ALLOW_BASE_SHLIBS}" = xyes -a -n "${Xshlibs_required_base}" ]; then
        echo "shlibs_required: [" >> ${PKG_NAME}.expected
        for i in ${Xshlibs_required_base}; do
            echo ${NL}"    "\"$i\" >> ${PKG_NAME}.expected
        done
        echo "]" >> ${PKG_NAME}.expected
    fi

    if [ -n "${XFreeBSD_version}" ]; then
    	cat << EOF >> ${PKG_NAME}.expected
annotations {
    FreeBSD_version = "${XFreeBSD_version}";
}
EOF
    fi

	cat << EOF >> ${PKG_NAME}.expected
files {${PKG_SHA256}
}
EOF
}


create_from_bin_body() {
    local PKG_NAME=testbin

    for bin in \
        freebsd-aarch64.bin freebsd-amd64.bin freebsd-armv6.bin freebsd-armv7.bin \
		freebsd-i386.bin freebsd-powerpc.bin freebsd-powerpc64.bin freebsd-powerpc64le.bin \
		freebsd-riscv64.bin dfly.bin linux.bin \
        macos.bin macos106.bin macos150.bin macosfat.bin \
		"macosfat.bin#amd64" "macosfat.bin#aarch64"
    do
        local file1=$(atf_get_srcdir)/$bin

        ALLOW_BASE_SHLIBS=no
        genmanifest ${PKG_NAME} ${file1}

		atf_check \
			-o inline:"${ALLOW_BASE_SHLIBS}\n" \
			pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=${file1} -o ALLOW_BASE_SHLIBS=${ALLOW_BASE_SHLIBS} config allow_base_shlibs

        # cat ${PKG_NAME}.manifest
        atf_check \
            -o empty \
            -e empty \
            -s exit:0 \
            pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=${file1} -o ALLOW_BASE_SHLIBS=${ALLOW_BASE_SHLIBS} create -M ./${PKG_NAME}.manifest -r ${TMPDIR}

        # cat ${PKG_NAME}.expected
        atf_check \
            -o file:${PKG_NAME}.expected \
            -e empty \
            -s exit:0 \
            pkg info -R --raw-format=ucl -F ${PKG_NAME}-1.pkg
    done
}

create_from_binbase_body() {
    local PKG_NAME=testbinbase

    for bin in \
        freebsd-aarch64.bin freebsd-amd64.bin freebsd-armv6.bin freebsd-armv7.bin \
		freebsd-i386.bin freebsd-powerpc.bin freebsd-powerpc64.bin freebsd-powerpc64le.bin \
		freebsd-riscv64.bin dfly.bin linux.bin \
        macos.bin macos106.bin macos150.bin macosfat.bin \
		"macosfat.bin#amd64" "macosfat.bin#aarch64"
    do
        local file1=$(atf_get_srcdir)/$bin

        ALLOW_BASE_SHLIBS=yes
        genmanifest ${PKG_NAME} ${file1}

        atf_check \
			-o inline:"${ALLOW_BASE_SHLIBS}\n" \
			pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=${file1} -o ALLOW_BASE_SHLIBS=${ALLOW_BASE_SHLIBS} config allow_base_shlibs

        # cat ${PKG_NAME}.manifest
        atf_check \
            -o empty \
            -e empty \
            -s exit:0 \
            pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=${file1} -o ALLOW_BASE_SHLIBS=${ALLOW_BASE_SHLIBS} create -M ./${PKG_NAME}.manifest -r ${TMPDIR}

        # cat ${PKG_NAME}.expected
        atf_check \
            -o file:${PKG_NAME}.expected \
            -e empty \
            -s exit:0 \
            pkg info -R --raw-format=ucl -F ${PKG_NAME}-1.pkg
    done
}