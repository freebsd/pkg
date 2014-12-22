#! /bin/sh
# Script to sign a package.
# Currently only pkg(7) supports checking for signed ports-mgmt/pkg package.
set -e

if [ $# -eq 0 ]; then
	echo "Usage: $0 pkg.txz" >&2
	exit 1
fi

pkg="$1"
sign_cmd="${2:-./sign.sh}"
rm -f "${pkg}.sig"
openssl dgst -sha256 -binary "${pkg}" | hexdump -v -e '/1 "%x"' | "${sign_cmd}" > "${pkg}.sig"
