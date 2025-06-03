

new_pkg() {
	cat << EOF > $1.ucl
name: $2
origin: $2
version: "$3"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: ${4}
abi: "*"
desc: <<EOD
This is a test
EOD
EOF
}

new_pkgf() {
	cat << EOF > $1.ucl
name: $2
origin: $3
version: "$4"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: ${5}
abi: "*"
desc: <<EOD
This is a test
EOD
EOF
}

new_manifest() {
	cat << EOF > +MANIFEST
name: "$1"
origin: $1"
version: "$2"
arch: "freebsd:*"
maintainer: "test"
prefix: "${3}"
www: "http://test"
comment: "a test"
desc: "This is a test"
EOF
}

SUBCMD=$1
shift
${SUBCMD} $*
