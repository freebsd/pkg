#!/bin/sh
exctags -f /dev/stdout --c-kinds=fp pkg.h | awk 'BEGIN { print "LIBPKG_1.4 {"; print "global:" } /^[^!]/ { print "\t"$1";" } END { print "# Symbols from libcsu\n\t__progname;\n\tenviron;\nlocal:\n\t*;\n};" }' > libpkg.ver

