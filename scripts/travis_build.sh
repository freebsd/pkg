#! /bin/sh
# Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer
#    in this position and unchanged.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This script is run as the 'script' step in .travis.yml.  Have a standalone
# script is better than listing multiple scripts in .travis.yml for several
# reasons:
# 1 - It allows for condition logic
# 2 - We can bail out on the first failure (set -x).  Travis will always try
#     to run all the scripts so, for example, it will try to run 'make' even
#     if 'configure' fails.
# 3 - It allows developers to easily, with a single command re-produce what
#     travis is doing.

set -x
set -e

if [ $(uname -s) = "Darwin" ]; then
  CFLAGS="-I/usr/local/opt/libarchive/include" \
    LDFLAGS="-L/usr/local/opt/libarchive/lib" \
    ./configure
elif [ $(uname -s) = "Linux" ]; then
  CFLAGS="-Wno-strict-aliasing -Wno-unused-result -Wno-unused-value" ./configure
fi

# Build quietly and in parallel first.  If the build fails re-run
# with verbosity and in serial, both of which which make build errors
# easier to interpret.
make -j4 || make V=1

if [ $(uname -s) = "Darwin" ]; then
  # TODO(sbc100): Figure out how to get kyua and atf installed on the
  # linux travis instances so we can run the tests.
  make check
fi
