#!/bin/sh
# Example signing script. See pkg-repo(8) for more information.
set -e

read -t 2 sum
[ -z "$sum" ] && exit 1
echo SIGNATURE
echo -n "$sum" | /usr/bin/openssl dgst -sign repo.key -sha256 -binary
echo
echo CERT
cat repo.pub
echo END
