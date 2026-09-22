> Coccinelle (http://coccinelle.lip6.fr/) is a program matching and
> transformation engine which provides the language SmPL (Semantic Patch
> Language) for specifying desired matches and transformations in C code.

.cocci files in this directory are copied (and some adapted) from
http://coccinelle.lip6.fr/rules/. the pkg directory contains .cocci files
written for pkg.

Installation
============

port:

	# make -C /usr/ports/devel/coccinelle install clean

package:

	# pkg install coccinelle

Usage
=====
From the pkg's source root (use _libpkg_ or _src_ as `$DIR`):

	% spatch -I . -I /usr/include -I /usr/local/include -I libpkg -I src       \
		-I external/blake2  -I external/include     \
		-I external/libelf  -I external/libfetch        \
		-I external/libsbuf -I external/libucl/include -I external/linenoise   \
		-I external/picosat -I external/sqlite         \
		-in_place -sp_file ./tests/cocci/$TESTFILE.cocci -dir $DIR
