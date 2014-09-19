#!/bin/sh
exctags -f /dev/stdout --c-kinds=fp pkg.h | awk 'BEGIN { print "LIBPKG_1.3 {"; print "global:" } /^[^!]/ { print "\t"$1";" } END { print "local:\n\t*;\n};" }' > libpkg.ver

