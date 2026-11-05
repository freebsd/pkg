#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	create_from_bin \
	create_from_bin_note_padding \
	create_from_bin_note_abi

genmanifest() {
    local PKG_NAME="$1"
    shift
    local PKG_FLATSIZE=0
    local PKG_FILES=""
    local PKG_SUM=""
    local NL="
"
    local hide_provided="$1"
    shift

    bin_meta "$1"
    while [ -n "$1" ]; do
        local file1="${1%#*}"
        local file1_base=$(basename ${file1})
        local file1_size=$(wc -c < ${file1})
        local file1_sum=$(pkg checksum -q -t blake2_base32 ${file1})
        cp -a ${file1} ${TMPDIR}/${file1_base}
	file1_mtime=$(q_mtime ${TMPDIR}/${file1_base})

        PKG_FILES="${PKG_FILES}/${file1_base}: {perm: 0644}${NL}"
	PKG_SUM="${PKG_SUM}
    /${file1_base} {
        sum = \"${file1_sum}\";
        uname = \"root\";
        gname = \"wheel\";
        perm = \"0644\";
        mtime = ${file1_mtime};
    }"

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
    if [ -n "${Xshlibs_required}" ]; then
        echo "shlibs_required [" >> ${PKG_NAME}.expected
        for i in ${Xshlibs_required}; do
            echo ${NL}"    "\"$i\", >> ${PKG_NAME}.expected
        done
        echo "]" >> ${PKG_NAME}.expected
    fi
    if [ -n "${Xshlibs_provided}" ]; then
        if [ -n "${hide_provided}" ]; then
            echo "shlibs_provided_ignore [" >> ${PKG_NAME}.expected
        else
            echo "shlibs_provided [" >> ${PKG_NAME}.expected
        fi
        for i in ${Xshlibs_provided}; do
            echo ${NL}"    "\"$i\", >> ${PKG_NAME}.expected
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
files {${PKG_SUM}
}
EOF
}

do_check() {
    local PKG_NAME=$1
    local file1=$(atf_get_srcdir)/$2
    local hide_provided=$3

    genmanifest ${PKG_NAME} "${hide_provided}" ${file1}

    # cat ${PKG_NAME}.manifest
    atf_check \
        -o empty \
        -e empty \
        -s exit:0 \
        pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=${file1} $hide_provided \
        create -M ./${PKG_NAME}.manifest -r ${TMPDIR}

    # cat ${PKG_NAME}.expected
    atf_check \
        -o file:${PKG_NAME}.expected \
        -e empty \
        -s exit:0 \
        pkg info -R --raw-format=ucl -F ${PKG_NAME}-1.pkg
}

# The note section of the fixture ends with padding shorter than a note
# header, or with an ABI note whose descriptor does not fit: parsing it
# must not read past the section data.
note_fixture_body() {
	local file1=$(atf_get_srcdir)/$1

	cat << EOF > note.manifest
name: notetest
origin: test/notetest
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /
desc: Yet another test
files: { /$1: "" }
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=${file1} \
		create -M ./note.manifest -r $(atf_get_srcdir) -o ${TMPDIR}
}

create_from_bin_note_padding_body() {
	note_fixture_body note-padding.bin
}

create_from_bin_note_abi_body() {
	note_fixture_body note-abi.bin
}

create_from_bin_body() {
    for bin in \
        freebsd-aarch64.bin freebsd-amd64.bin freebsd-armv6.bin freebsd-armv7.bin \
		freebsd-i386.bin freebsd-loongarch64.bin freebsd-powerpc.bin freebsd-powerpc64.bin freebsd-powerpc64le.bin \
		freebsd-riscv64.bin dfly.bin linux.bin musl.bin \
        macos.bin macos106.bin macos150.bin \
        macosfat.bin "macosfat.bin#x86_64" "macosfat.bin#aarch64" \
        macosfatlib.bin "macosfatlib.bin#x86_64" "macosfatlib.bin#aarch64"
    do
        do_check testbin $bin
        do_check testbin $bin "-o SHLIB_PROVIDE_PATHS_NATIVE=/does/not/exist"
        do_check testbin $bin "-o SHLIB_PROVIDE_IGNORE_GLOB=*"
    done
}
